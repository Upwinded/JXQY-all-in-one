#include "MapEditorWindow.h"
#include "MapRenderCanvas.h"
#include "MpcPreviewLabel.h"
#include "EntityPropertyDialog.h"
#include "MinimapWidget.h"
#include "TrapScriptEditorDialog.h"
#include "ui_MapEditorWindow.h"
#include "../core/Util.h"
#include "../core/AuthoringMutationGate.h"
#include "../core/ImageResourceCandidates.h"
#include "../core/DurableFileTransaction.h"

#include <algorithm>
#include <climits>
#include <QFileDialog>
#include <QInputDialog>
#include <QScrollArea>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QToolBar>
#include <QActionGroup>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStatusBar>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenu>
#include <QMenuBar>
#include <QEvent>
#include <QTableWidget>
#include <QHeaderView>
#include <QToolButton>
#include <QLineEdit>
#include <QPainter>
#include <QTabWidget>
#include <QUuid>

namespace
{
// MPC 数值字段（动态/障碍/序号）的统一校验策略。
// 需求：保留未修改的历史异常值（如动态=13988396、障碍=13987372），
// 仅在用户主动编辑时做范围校验，对超出范围的编辑拒绝提交，而不是静默夹取。
//   - 未修改（输入文本与原始值的字符串形式一致）：写回原始值，返回 Unmodified。
//   - 已修改且解析成功并在 [min,max] 范围内：采用新值，返回 Valid。
//   - 已修改但无法解析或超出范围：返回 Invalid，调用方应中止本次提交。
// 单槽编辑器（QSpinBox 文本）与 MPC 管理器（QTableWidget 单元格文本）共用此逻辑，
// 保证两条写入路径策略一致。
enum class MpcFieldResult { Unmodified, Valid, Invalid };
MpcFieldResult resolveMpcIntegerField(const QString& text, int originalValue,
                                      int min, int max, int& outValue)
{
    if (text.trimmed() == QString::number(originalValue))
    {
        outValue = originalValue;
        return MpcFieldResult::Unmodified;
    }
    bool ok = false;
    int parsed = text.trimmed().toInt(&ok);
    if (ok && parsed >= min && parsed <= max)
    {
        outValue = parsed;
        return MpcFieldResult::Valid;
    }
    return MpcFieldResult::Invalid;
}

Qt::CaseSensitivity managedPathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalizedAbsolutePath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isPathLexicallyInsideDirectory(const QString& basePath, const QString& candidatePath)
{
    if (basePath.isEmpty() || candidatePath.isEmpty())
        return false;

    const QString base = normalizedAbsolutePath(basePath);
    const QString candidate = normalizedAbsolutePath(candidatePath);
    const Qt::CaseSensitivity caseSensitivity = managedPathCaseSensitivity();
    return candidate.compare(base, caseSensitivity) == 0 ||
           candidate.startsWith(base + QDir::separator(), caseSensitivity) ||
           candidate.startsWith(base + '/', caseSensitivity);
}

bool isPathInsideDirectory(const QString& basePath, const QString& candidatePath)
{
    if (basePath.isEmpty() || candidatePath.isEmpty())
        return false;

    const QString base = normalizedAbsolutePath(basePath);
    const QString candidate = normalizedAbsolutePath(candidatePath);
    if (!isPathLexicallyInsideDirectory(base, candidate))
        return false;

    // Resolve the deepest existing ancestor as well, so a symlink/junction
    // inside assets cannot redirect a future file into an outside directory.
    QString canonicalBase = QFileInfo(base).canonicalFilePath();
    if (canonicalBase.isEmpty())
        canonicalBase = base;
    QString probe = candidate;
    while (!QFileInfo::exists(probe))
    {
        QString parent = QFileInfo(probe).dir().absolutePath();
        if (parent == probe)
            break;
        probe = parent;
    }
    QString canonicalProbe = QFileInfo(probe).canonicalFilePath();
    if (canonicalProbe.isEmpty())
        canonicalProbe = normalizedAbsolutePath(probe);
    return isPathLexicallyInsideDirectory(
        QDir::cleanPath(canonicalBase), QDir::cleanPath(canonicalProbe));
}

bool isRuntimeSaveListPath(const QString& assetsBasePath,
                           const QString& candidatePath)
{
    return !assetsBasePath.isEmpty() &&
        isPathInsideDirectory(
            QDir(assetsBasePath).filePath("save/game"), candidatePath);
}

struct ManagedPathLess
{
    bool operator()(const QString& left, const QString& right) const
    {
        return QString::compare(left, right, managedPathCaseSensitivity()) < 0;
    }
};

struct SharedPendingMpcResource
{
    const MapEditorWindow* creator = nullptr;
    QByteArray bytes;
    std::set<const MapEditorWindow*> referencingWindows;
    bool durable = false;
};

using SharedPendingMpcRegistry =
    std::map<QString, SharedPendingMpcResource, ManagedPathLess>;

SharedPendingMpcRegistry& sharedPendingMpcRegistry()
{
    // Deliberately process-lifetime: editor windows can be destroyed during
    // QApplication teardown, after function-static destruction order becomes
    // difficult to guarantee.
    static auto* registry = new SharedPendingMpcRegistry();
    return *registry;
}

SharedPendingMpcResource* findSharedPendingMpcResource(const QString& path)
{
    auto& registry = sharedPendingMpcRegistry();
    auto iterator = registry.find(normalizedAbsolutePath(path));
    return iterator == registry.end() ? nullptr : &iterator->second;
}

bool sharedPendingBytesConflict(const QString& path, const QByteArray& bytes)
{
    SharedPendingMpcResource* resource = findSharedPendingMpcResource(path);
    return resource && resource->bytes != bytes;
}

bool registerSharedPendingMpcResource(const QString& path,
                                      const QByteArray& bytes,
                                      const MapEditorWindow* creator)
{
    const QString key = normalizedAbsolutePath(path);
    auto& registry = sharedPendingMpcRegistry();
    auto iterator = registry.find(key);
    if (iterator == registry.end())
    {
        SharedPendingMpcResource resource;
        resource.creator = creator;
        resource.bytes = bytes;
        registry.emplace(key, std::move(resource));
        return true;
    }

    if (iterator->second.bytes != bytes)
        return false;

    // Another window recreated or adopted an undo-only payload. From this
    // point the file is shared and the original window must never delete it.
    if (iterator->second.creator != creator)
        iterator->second.durable = true;
    return iterator->second.creator == creator;
}

void updateSharedPendingMpcReferences(
    const MapEditorWindow* window,
    const std::set<QString, ManagedPathLess>& referencedPaths)
{
    auto& registry = sharedPendingMpcRegistry();
    for (auto& entry : registry)
        entry.second.referencingWindows.erase(window);

    for (const QString& path : referencedPaths)
    {
        auto iterator = registry.find(normalizedAbsolutePath(path));
        if (iterator == registry.end())
            continue;
        iterator->second.referencingWindows.insert(window);
    }
}

bool sharedPendingMpcResourceIsDurable(const QString& path,
                                       const MapEditorWindow* owner)
{
    SharedPendingMpcResource* resource = findSharedPendingMpcResource(path);
    if (!resource)
        return false;
    if (resource->durable || resource->creator != owner)
        return true;
    for (const MapEditorWindow* window : resource->referencingWindows)
    {
        if (window != owner)
            return true;
    }
    return false;
}

void markSharedPendingMpcResourceDurable(const QString& path)
{
    if (SharedPendingMpcResource* resource = findSharedPendingMpcResource(path))
        resource->durable = true;
}

void releaseSharedPendingMpcResource(const QString& path,
                                     const MapEditorWindow* owner)
{
    auto& registry = sharedPendingMpcRegistry();
    auto iterator = registry.find(normalizedAbsolutePath(path));
    if (iterator != registry.end() && iterator->second.creator == owner)
        registry.erase(iterator);
}

void removeWindowFromSharedPendingMpcRegistry(const MapEditorWindow* window)
{
    auto& registry = sharedPendingMpcRegistry();
    for (auto& entry : registry)
        entry.second.referencingWindows.erase(window);
}
} // namespace

MapEditorWindow::MapEditorWindow(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::MapEditorWindow)
{
    ui->setupUi(this);

    canvas = new MapRenderCanvas(this);

    setupMenuBar();
    setupToolbar();
    setupConnections();

    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setObjectName(QStringLiteral("mapMainSplitter"));

    QWidget* leftPanel = new QWidget;
    leftPanel->setObjectName(QStringLiteral("mapPaintTaskPage"));
    paintTaskPage = leftPanel;
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(4, 4, 4, 4);

    QGroupBox* layerGroup = new QGroupBox(tr("图层"));
    QVBoxLayout* layerOuterLayout = new QVBoxLayout(layerGroup);
    layerOuterLayout->setContentsMargins(4, 4, 4, 4);
    layerOuterLayout->setSpacing(2);

    QCheckBox* layer0Check = new QCheckBox(tr("地面层"));
    layer0Check->setChecked(true);
    QCheckBox* layer1Check = new QCheckBox(tr("建筑层"));
    layer1Check->setChecked(true);
    QCheckBox* layer2Check = new QCheckBox(tr("空中层"));
    layer2Check->setChecked(true);

    QCheckBox* obstacleCheck = new QCheckBox(tr("障碍"));
    obstacleCheck->setChecked(true);
    QCheckBox* trapCheck = new QCheckBox(tr("陷阱"));
    trapCheck->setChecked(true);
    QCheckBox* npcCheck = new QCheckBox(tr("NPC"));
    npcCheck->setChecked(true);
    QCheckBox* objectCheck = new QCheckBox(tr("物体"));
    objectCheck->setChecked(true);
    QCheckBox* gridCheck = new QCheckBox(tr("网格"));
    gridCheck->setChecked(true);
    QCheckBox* coordinateCheck = new QCheckBox(tr("坐标"));
    coordinateCheck->setChecked(true);
    coordinateCheck->setToolTip(tr("在瓦片上显示坐标（缩放较小时会抽样显示）"));
    coordinateVisibleCheck = coordinateCheck;

    // Tile layers sub-section
    QLabel* tileLayerLabel = new QLabel(tr("瓦片图层:"));
    tileLayerLabel->setStyleSheet("font-weight: bold; font-size: 10px;");
    layerOuterLayout->addWidget(tileLayerLabel);

    QHBoxLayout* tileLayerRow = new QHBoxLayout();
    tileLayerRow->setSpacing(4);
    tileLayerRow->addWidget(layer0Check);
    tileLayerRow->addWidget(layer1Check);
    tileLayerRow->addWidget(layer2Check);
    tileLayerRow->addStretch();
    layerOuterLayout->addLayout(tileLayerRow);

    // Display toggles sub-section
    QLabel* displayLabel = new QLabel(tr("辅助显示:"));
    displayLabel->setStyleSheet("font-weight: bold; font-size: 10px;");
    layerOuterLayout->addWidget(displayLabel);

    QGridLayout* displayGrid = new QGridLayout();
    displayGrid->setHorizontalSpacing(6);
    displayGrid->setVerticalSpacing(1);
    displayGrid->addWidget(obstacleCheck, 0, 0);
    displayGrid->addWidget(trapCheck, 0, 1);
    displayGrid->addWidget(npcCheck, 1, 0);
    displayGrid->addWidget(objectCheck, 1, 1);
    displayGrid->addWidget(gridCheck, 2, 0);
    displayGrid->addWidget(coordinateCheck, 2, 1);
    layerOuterLayout->addLayout(displayGrid);

    QGroupBox* paintGroup = new QGroupBox(tr("绘制设置"));
    QFormLayout* paintLayout = new QFormLayout(paintGroup);
    paintSettingsLayout = paintLayout;
    // 让 field（预览/帧列表）能随窗体垂直扩展
    paintLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    paintLayerCombo = new QComboBox();
    paintLayerCombo->setObjectName("mapPaintLayerCombo");
    paintLayerCombo->addItem(tr("地面层"), 0);
    paintLayerCombo->addItem(tr("建筑层"), 1);
    paintLayerCombo->addItem(tr("空中层"), 2);
    paintLayerCombo->addItem(tr("全部图层"), -1);

    rectangularAreaSelectCheck = new QCheckBox(tr("方形区域选择"));
    rectangularAreaSelectCheck->setObjectName("mapRectangularAreaSelectCheck");
    rectangularAreaSelectCheck->setToolTip(tr(
        "勾选后下一次区域选择使用视觉矩形（staggeredX = 2*tileX + (tileY&1)），"
        "不会为了每行 Tile 数一致而向外扩展。未勾选使用菱形（UPEdit 风格）。"
        "切换只影响下一次新选择。"));
    rectangularAreaSelectCheck->setChecked(false);

    mpcComboBox = new QComboBox();
    mpcComboBox->setObjectName("mapMpcComboBox");
    frameSpinBox = new QSpinBox();
    frameSpinBox->setObjectName("mapFrameSpinBox");
    frameSpinBox->setRange(0, 255);
    frameSpinBox->setValue(0);

    obstacleComboBox = new QComboBox();
    obstacleComboBox->setObjectName("mapObstacleComboBox");
    obstacleComboBox->addItem(tr("可通过 (清除)"), 0x00);
    obstacleComboBox->addItem(tr("透明 (0x40)"), 0x40);
    obstacleComboBox->addItem(tr("跳透 (0x60)"), 0x60);
    obstacleComboBox->addItem(tr("实体 (0x80)"), 0x80);
    obstacleComboBox->addItem(tr("跳障 (0xA0)"), 0xA0);

    trapIndexSpinBox = new QSpinBox();
    trapIndexSpinBox->setObjectName("mapTrapIndexSpinBox");
    trapIndexSpinBox->setRange(0, 19);
    trapIndexSpinBox->setValue(0);
    trapIndexSpinBox->setSpecialValueText(tr("无（清除）"));
    trapIndexSpinBox->setToolTip(tr("0=无（清除），1-19=陷阱脚本索引"));

    paintLayout->addRow(tr("绘制图层:"), paintLayerCombo);
    paintLayout->addRow(tr("区域形状:"), rectangularAreaSelectCheck);

    // MPC 索引行：下拉选择 + 添加/编辑/删除按钮（与菜单 MPC 管理器共用业务逻辑）。
    QWidget* mpcRowWidget = new QWidget();
    mpcRowWidget->setObjectName(QStringLiteral("mapPaintMpcRow"));
    paintMpcRowWidget = mpcRowWidget;
    QHBoxLayout* mpcRowLayout = new QHBoxLayout(mpcRowWidget);
    mpcRowLayout->setContentsMargins(0, 0, 0, 0);
    mpcRowLayout->setSpacing(2);
    mpcRowLayout->addWidget(mpcComboBox, 1);

    QPushButton* addMpcSlotButtonLocal = new QPushButton(tr("+"));
    addMpcSlotButtonLocal->setObjectName("mapAddMpcSlotButton");
    addMpcSlotButtonLocal->setToolTip(tr("添加 MPC：选择 .mpc 文件并占用第一个空槽"));
    addMpcSlotButtonLocal->setFixedSize(26, 24);
    QPushButton* editMpcSlotButtonLocal = new QPushButton(tr("✎"));
    editMpcSlotButtonLocal->setObjectName("mapEditMpcSlotButton");
    editMpcSlotButtonLocal->setToolTip(tr("编辑 MPC：替换资源或修改 name/dynamic/obstacle/index（保持槽位号不变）"));
    editMpcSlotButtonLocal->setFixedSize(26, 24);
    QPushButton* deleteMpcSlotButtonLocal = new QPushButton(tr("−"));
    deleteMpcSlotButtonLocal->setObjectName("mapDeleteMpcSlotButton");
    deleteMpcSlotButtonLocal->setToolTip(tr("删除 MPC：清空当前槽位并清理引用（不压缩槽位，需确认）"));
    deleteMpcSlotButtonLocal->setFixedSize(26, 24);
    addMpcSlotButton = addMpcSlotButtonLocal;
    editMpcSlotButton = editMpcSlotButtonLocal;
    deleteMpcSlotButton = deleteMpcSlotButtonLocal;
    mpcRowLayout->addWidget(addMpcSlotButtonLocal);
    mpcRowLayout->addWidget(editMpcSlotButtonLocal);
    mpcRowLayout->addWidget(deleteMpcSlotButtonLocal);

    paintLayout->addRow(tr("MPC索引:"), mpcRowWidget);
    paintLayout->addRow(tr("帧索引:"), frameSpinBox);
    paintLayout->addRow(tr("障碍类型:"), obstacleComboBox);
    paintLayout->addRow(tr("陷阱索引:"), trapIndexSpinBox);

    tilePreviewLabel = new MpcPreviewLabel();
    tilePreviewLabel->setObjectName("mapTilePreviewLabel");
    tilePreviewLabel->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    tilePreviewLabel->clearImage(tr("瓦片预览"));
    paintLayout->addRow(tr("预览:"), tilePreviewLabel);

    // 帧预览网格：显示当前 MPC 的所有帧缩略图
    framePreviewScrollArea = new QScrollArea();
    framePreviewScrollArea->setWidgetResizable(true);
    framePreviewScrollArea->setMinimumHeight(180);
    framePreviewScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    framePreviewScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    QWidget* framePreviewContainer = new QWidget();
    framePreviewGrid = new QGridLayout(framePreviewContainer);
    framePreviewGrid->setContentsMargins(2, 2, 2, 2);
    framePreviewGrid->setSpacing(2);
    framePreviewContainer->setLayout(framePreviewGrid);
    framePreviewScrollArea->setWidget(framePreviewContainer);
    paintLayout->addRow(tr("帧列表:"), framePreviewScrollArea);

    // paintGroup 占据左侧剩余空间，让绘制设置和帧列表随窗体扩展
    leftLayout->addWidget(paintGroup, 1);

    leftPanel->setMinimumWidth(260);
    leftPanel->setMaximumWidth(QWIDGETSIZE_MAX);

    // --- Entity task page ---
    QWidget* entityModule = new QWidget;
    entityModule->setObjectName(QStringLiteral("mapEntityTaskPage"));
    entityTaskPage = entityModule;
    QVBoxLayout* entityModuleLayout = new QVBoxLayout(entityModule);
    entityModuleLayout->setContentsMargins(4, 4, 4, 4);

    entityTabs = new QTabWidget();
    entityTabs->setObjectName("mapEntityTabs");

    // NPC tab
    QWidget* npcTab = new QWidget;
    QVBoxLayout* npcTabLayout = new QVBoxLayout(npcTab);
    npcSearchEdit = new QLineEdit();
    npcSearchEdit->setObjectName("mapNpcSearchEdit");
    npcSearchEdit->setPlaceholderText(tr("搜索NPC..."));
    npcListWidget = new QListWidget();
    npcListWidget->setObjectName("mapNpcListWidget");
    npcListWidget->setMinimumHeight(120);
    QHBoxLayout* npcButtonRow1 = new QHBoxLayout();
    QPushButton* addNpcButton = new QPushButton(tr("添加"));
    QPushButton* deleteNpcButton = new QPushButton(tr("删除"));
    QPushButton* locateNpcButton = new QPushButton(tr("定位"));
    QPushButton* propNpcButton = new QPushButton(tr("属性"));
    npcButtonRow1->addWidget(addNpcButton);
    npcButtonRow1->addWidget(deleteNpcButton);
    npcButtonRow1->addWidget(locateNpcButton);
    npcButtonRow1->addWidget(propNpcButton);

    QHBoxLayout* npcButtonRow2 = new QHBoxLayout();
    QPushButton* loadNpcButton = new QPushButton(tr("加载"));
    QPushButton* saveNpcButton = new QPushButton(tr("保存"));
    QPushButton* saveAsNpcButton = new QPushButton(tr("另存"));
    QPushButton* closeNpcButton = new QPushButton(tr("关闭"));
    npcButtonRow2->addWidget(loadNpcButton);
    npcButtonRow2->addWidget(saveNpcButton);
    npcButtonRow2->addWidget(saveAsNpcButton);
    npcButtonRow2->addWidget(closeNpcButton);

    npcTabLayout->addWidget(npcSearchEdit);
    npcTabLayout->addWidget(npcListWidget, 1);
    npcTabLayout->addLayout(npcButtonRow1);
    npcTabLayout->addLayout(npcButtonRow2);
    entityTabs->addTab(npcTab, tr("NPC"));

    // Object tab
    QWidget* objTab = new QWidget;
    QVBoxLayout* objTabLayout = new QVBoxLayout(objTab);
    objectSearchEdit = new QLineEdit();
    objectSearchEdit->setObjectName("mapObjectSearchEdit");
    objectSearchEdit->setPlaceholderText(tr("搜索物体..."));
    objectListWidget = new QListWidget();
    objectListWidget->setObjectName("mapObjectListWidget");
    objectListWidget->setMinimumHeight(120);
    QHBoxLayout* objButtonRow1 = new QHBoxLayout();
    QPushButton* addObjButton = new QPushButton(tr("添加"));
    QPushButton* deleteObjButton = new QPushButton(tr("删除"));
    QPushButton* locateObjButton = new QPushButton(tr("定位"));
    QPushButton* propObjButton = new QPushButton(tr("属性"));
    objButtonRow1->addWidget(addObjButton);
    objButtonRow1->addWidget(deleteObjButton);
    objButtonRow1->addWidget(locateObjButton);
    objButtonRow1->addWidget(propObjButton);

    QHBoxLayout* objButtonRow2 = new QHBoxLayout();
    QPushButton* loadObjButton = new QPushButton(tr("加载"));
    QPushButton* saveObjButton = new QPushButton(tr("保存"));
    QPushButton* saveAsObjButton = new QPushButton(tr("另存"));
    QPushButton* closeObjButton = new QPushButton(tr("关闭"));
    objButtonRow2->addWidget(loadObjButton);
    objButtonRow2->addWidget(saveObjButton);
    objButtonRow2->addWidget(saveAsObjButton);
    objButtonRow2->addWidget(closeObjButton);

    objTabLayout->addWidget(objectSearchEdit);
    objTabLayout->addWidget(objectListWidget, 1);
    objTabLayout->addLayout(objButtonRow1);
    objTabLayout->addLayout(objButtonRow2);
    entityTabs->addTab(objTab, tr("物体"));

    entityModuleLayout->addWidget(entityTabs, 1);

    continuousPlaceCheck = new QCheckBox(tr("连续放置"));
    continuousPlaceCheck->setObjectName("mapContinuousPlaceCheck");
    continuousPlaceCheck->setToolTip(tr("放置实体后保持放置模式，可连续放置多个"));

    saveNpcWithMapCheck = new QCheckBox(tr("保存地图时同时保存NPC"));
    saveNpcWithMapCheck->setObjectName("mapSaveNpcWithMapCheck");
    saveObjWithMapCheck = new QCheckBox(tr("保存地图时同时保存物体"));
    saveObjWithMapCheck->setObjectName("mapSaveObjectWithMapCheck");

    // Load persisted settings (default: false to avoid accidental overwrites)
    {
        QSettings settings("JXQY", "MapEditor");
        saveNpcWithMapCheck->setChecked(settings.value("saveNpcWithMap", false).toBool());
        saveObjWithMapCheck->setChecked(settings.value("saveObjWithMap", false).toBool());
    }
    saveNpcWithMapCheck->setToolTip(tr("关闭后保存地图不会覆写NPC列表文件"));
    saveObjWithMapCheck->setToolTip(tr("关闭后保存地图不会覆写物体列表文件"));

    entityModuleLayout->addWidget(continuousPlaceCheck);
    entityModuleLayout->addWidget(saveNpcWithMapCheck);
    entityModuleLayout->addWidget(saveObjWithMapCheck);

    entityFilePathLabel = new QLabel(tr("NPC: 未加载 | 物体: 未加载"));
    entityFilePathLabel->setObjectName("mapEntityFilePathLabel");
    entityFilePathLabel->setWordWrap(true);
    entityModuleLayout->addWidget(entityFilePathLabel);

    QGroupBox* entityInfoGroup = new QGroupBox(tr("属性"));
    entityInfoGroup->setObjectName(QStringLiteral("mapEntityInfoGroup"));
    QVBoxLayout* entityInfoLayout = new QVBoxLayout(entityInfoGroup);

    entityInfoTree = new QTreeWidget();
    entityInfoTree->setObjectName(QStringLiteral("mapEntityInfoTree"));
    entityInfoTree->setHeaderLabels({tr("属性"), tr("值")});
    entityInfoTree->setMinimumHeight(120);
    entityInfoLayout->addWidget(entityInfoTree, 1);

    // 实体资源预览与实体列表、属性放在同一任务页，避免选择后跨栏查找。
    entityPreviewLabel = new QLabel();
    entityPreviewLabel->setObjectName("mapEntityPreviewLabel");
    entityPreviewLabel->setAlignment(Qt::AlignCenter);
    entityPreviewLabel->setMinimumHeight(64);
    entityPreviewLabel->setMaximumHeight(160);
    entityPreviewLabel->setStyleSheet("QLabel { background-color: #2b2b2b; border: 1px solid #555; }");
    entityPreviewLabel->hide();
    entityInfoLayout->addWidget(entityPreviewLabel);
    entityModuleLayout->addWidget(entityInfoGroup, 1);

    entityModule->setMinimumWidth(220);
    entityModule->setMaximumWidth(QWIDGETSIZE_MAX);

    // --- Inspect task page: layer visibility + tile info + minimap ---
    QWidget* rightPanel = new QWidget;
    rightPanel->setObjectName(QStringLiteral("mapInspectTaskPage"));
    inspectTaskPage = rightPanel;
    QVBoxLayout* rightBottomLayout = new QVBoxLayout(rightPanel);
    rightBottomLayout->setContentsMargins(4, 4, 4, 4);
    rightBottomLayout->addWidget(layerGroup);

    QGroupBox* tileInfoGroup = new QGroupBox(tr("瓦片信息"));
    QVBoxLayout* tileInfoLayout = new QVBoxLayout(tileInfoGroup);

    tileInfoTree = new QTreeWidget();
    tileInfoTree->setObjectName("mapTileInfoTree");
    tileInfoTree->setHeaderLabels({tr("属性"), tr("值")});
    tileInfoLayout->addWidget(tileInfoTree);

    rightBottomLayout->addWidget(tileInfoGroup, 1);

    QGroupBox* minimapGroup = new QGroupBox(tr("小地图"));
    QVBoxLayout* minimapLayout = new QVBoxLayout(minimapGroup);

    minimapWidget = new MinimapWidget();
    minimapWidget->setObjectName("mapMinimapWidget");
    minimapLayout->addWidget(minimapWidget);

    rightBottomLayout->addWidget(minimapGroup);

    rightPanel->setMinimumWidth(240);

    // 单一任务侧栏：画布始终占据主空间，侧栏按工具显示绘制、实体或查看。
    taskPanel = new QTabWidget();
    taskPanel->setObjectName(QStringLiteral("mapTaskPanel"));
    taskPanel->setDocumentMode(true);
    taskPanel->setMinimumWidth(280);
    taskPanel->setMaximumWidth(440);
    taskPanel->addTab(paintTaskPage, tr("绘制"));
    taskPanel->addTab(entityTaskPage, tr("NPC / 物体"));
    taskPanel->addTab(inspectTaskPage, tr("查看"));
    taskPanel->setCurrentWidget(inspectTaskPage);

    mainSplitter->addWidget(canvas);
    mainSplitter->addWidget(taskPanel);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 0);
    mainSplitter->setSizes({880, 320});

    // 隐藏页仍保留上一次绘制参数；首次进入绘制时从最常用的瓦片工具开始。
    updatePaintSettingsForTool(MapEditTool::TilePaint);

    // 复用 .ui 中的 mainLayout，不再 new QVBoxLayout(this)
    ui->mainLayout->addWidget(mainSplitter, 1);

    connect(layer0Check, &QCheckBox::toggled, this, [this](bool checked) { onLayerVisibilityChanged(0, checked); });
    connect(layer1Check, &QCheckBox::toggled, this, [this](bool checked) { onLayerVisibilityChanged(1, checked); });
    connect(layer2Check, &QCheckBox::toggled, this, [this](bool checked) { onLayerVisibilityChanged(2, checked); });
    connect(obstacleCheck, &QCheckBox::toggled, this, &MapEditorWindow::onObstacleVisibilityChanged);
    connect(trapCheck, &QCheckBox::toggled, this, &MapEditorWindow::onTrapVisibilityChanged);
    connect(npcCheck, &QCheckBox::toggled, this, &MapEditorWindow::onNpcVisibilityChanged);
    connect(objectCheck, &QCheckBox::toggled, this, &MapEditorWindow::onObjectVisibilityChanged);
    connect(gridCheck, &QCheckBox::toggled, this, &MapEditorWindow::onGridVisibilityChanged);
    connect(coordinateCheck, &QCheckBox::toggled, this, &MapEditorWindow::onCoordinateVisibilityChanged);
    connect(paintLayerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MapEditorWindow::onPaintLayerChanged);
    connect(rectangularAreaSelectCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (canvas)
            canvas->setRectangularAreaSelect(checked);
    });
    connect(mpcComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MapEditorWindow::onMpcSelectionChanged);
    // 左侧 MPC 索引快速增删改按钮（与菜单 MPC 管理器共用业务逻辑）
    if (addMpcSlotButton)
        connect(addMpcSlotButton, &QPushButton::clicked, this, &MapEditorWindow::onAddMpcSlot);
    if (editMpcSlotButton)
        connect(editMpcSlotButton, &QPushButton::clicked, this, &MapEditorWindow::onEditMpcSlot);
    if (deleteMpcSlotButton)
        connect(deleteMpcSlotButton, &QPushButton::clicked, this, &MapEditorWindow::onDeleteMpcSlot);
    connect(frameSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapEditorWindow::onFrameSelectionChanged);
    connect(obstacleComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MapEditorWindow::onObstacleTypeChanged);
    connect(trapIndexSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapEditorWindow::onTrapIndexChanged);
    connect(npcListWidget, &QListWidget::currentRowChanged, this, &MapEditorWindow::onNpcListSelectionChanged);
    connect(objectListWidget, &QListWidget::currentRowChanged, this, &MapEditorWindow::onObjectListSelectionChanged);
    connect(npcListWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item)
    {
        if (item)
        {
            int entityIndex = item->data(Qt::UserRole).toInt();
            onEditEntityProperties(entityIndex, true);
        }
    });
    connect(objectListWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item)
    {
        if (item)
        {
            int entityIndex = item->data(Qt::UserRole).toInt();
            onEditEntityProperties(entityIndex, false);
        }
    });
    connect(addNpcButton, &QPushButton::clicked, this, &MapEditorWindow::onAddNpc);
    connect(addObjButton, &QPushButton::clicked, this, &MapEditorWindow::onAddObject);
    connect(deleteNpcButton, &QPushButton::clicked, this, &MapEditorWindow::onDeleteEntity);
    connect(npcSearchEdit, &QLineEdit::textChanged, this, &MapEditorWindow::updateNpcListWidget);
    connect(objectSearchEdit, &QLineEdit::textChanged, this, &MapEditorWindow::updateObjectListWidget);
    connect(continuousPlaceCheck, &QCheckBox::toggled, this, [this](bool checked) {
        canvas->setContinuousPlace(checked);
    });
    connect(deleteObjButton, &QPushButton::clicked, this, &MapEditorWindow::onDeleteEntity);
    connect(loadNpcButton, &QPushButton::clicked, this, [this]() { ui->actionLoadNpcList->trigger(); });
    connect(saveNpcButton, &QPushButton::clicked, this, &MapEditorWindow::onSaveNpcList);
    connect(saveAsNpcButton, &QPushButton::clicked, this, &MapEditorWindow::onSaveNpcListAs);
    connect(closeNpcButton, &QPushButton::clicked, this, &MapEditorWindow::onCloseNpcList);
    connect(locateNpcButton, &QPushButton::clicked, this, &MapEditorWindow::onLocateSelectedEntity);
    connect(propNpcButton, &QPushButton::clicked, this, [this]() {
        int index = canvas->getSelectedEntityIndex();
        if (index >= 0)
            onEditEntityProperties(index, true);
    });
    connect(loadObjButton, &QPushButton::clicked, this, [this]() { ui->actionLoadObjectList->trigger(); });
    connect(saveObjButton, &QPushButton::clicked, this, &MapEditorWindow::onSaveObjectList);
    connect(saveAsObjButton, &QPushButton::clicked, this, &MapEditorWindow::onSaveObjectListAs);
    connect(closeObjButton, &QPushButton::clicked, this, &MapEditorWindow::onCloseObjectList);
    connect(locateObjButton, &QPushButton::clicked, this, &MapEditorWindow::onLocateSelectedEntity);
    connect(propObjButton, &QPushButton::clicked, this, [this]() {
        int index = canvas->getSelectedEntityIndex();
        if (index >= 0)
            onEditEntityProperties(index, false);
    });

    canvas->setMapFileEditor(&mapEditor);
    canvas->setMpcImageCache(&mpcCache);

    minimapWidget->setMapFileEditor(&mapEditor);
    minimapWidget->setMpcImageCache(&mpcCache);
    minimapWidget->setCanvas(canvas);

    statusCoordLabel = new QLabel("X:- Y:-", this);
    statusZoomLabel = new QLabel("100%", this);
    statusMapSizeLabel = new QLabel("", this);
    statusToolLabel = new QLabel(tr("选择"), this);
    statusToolLabel->setObjectName("mapStatusToolLabel");
    statusEntityCountLabel = new QLabel("", this);

    auto statusBar = new QStatusBar(this);
    statusBar->addWidget(statusCoordLabel);
    statusBar->addWidget(statusZoomLabel);
    statusBar->addWidget(statusMapSizeLabel);
    statusBar->addWidget(statusToolLabel);
    statusBar->addWidget(statusEntityCountLabel);
    ui->mainLayout->addWidget(statusBar);

    connect(&undoRedoManager, &UndoRedoManager::undoStackChanged, this, &MapEditorWindow::onUndoStackChanged);

    // 浮动工具条：编辑工具放在画布左上角
    // 默认"地面层"绘制；Fill 不支持全部图层时在状态栏提示
    paintLayerCombo->setCurrentIndex(0); // "地面层"
    canvas->setPaintAllLayers(false);

    setupFloatingToolBar();

    // eventFilter：处理 canvas resize 以重定位浮动工具条
    canvas->installEventFilter(this);

    // eventFilter：帧预览滚动区域 resize 时重新布局帧网格
    framePreviewScrollArea->installEventFilter(this);

    // 主预览控件 MpcPreviewLabel 自带 resizeEvent 适配，不需要外层 eventFilter。

    updateEntityTabs();

    QSettings settings("JXQY", "MapEditor");
    restoreGeometry(settings.value("geometry").toByteArray());
    if (settings.contains("maximized") && settings.value("maximized").toBool())
        showMaximized();
}

MapEditorWindow::~MapEditorWindow()
{
    // closeEvent normally performs this cleanup. Keep destruction safe for
    // direct QObject/parent teardown as well, without showing modal UI while
    // the widget hierarchy is being destroyed.
    discardPendingMpcResources(false);
    removeWindowFromSharedPendingMpcRegistry(this);
    delete ui;
}

MapEditorWindow::EditingViewState
MapEditorWindow::editingViewState() const
{
    EditingViewState state;
    if (!canvas)
        return state;

    const QPoint scrollOffset =
        canvas->viewportScrollOffset();
    state.zoomLevel = canvas->getZoomLevel();
    state.scrollX = scrollOffset.x();
    state.scrollY = scrollOffset.y();
    return state;
}

bool MapEditorWindow::restoreEditingViewState(
    const EditingViewState& state)
{
    return canvas && canvas->restoreViewport(
        static_cast<float>(state.zoomLevel),
        QPoint(state.scrollX, state.scrollY));
}

void MapEditorWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        retranslateDynamicUi();
    }
    QWidget::changeEvent(event);
}

void MapEditorWindow::closeEvent(QCloseEvent* event)
{
    if (consumePreparedClose())
    {
        QSettings settings("JXQY", "MapEditor");
        settings.setValue("geometry", saveGeometry());
        settings.setValue("maximized", isMaximized());
        settings.setValue("saveNpcWithMap", saveNpcWithMapCheck->isChecked());
        settings.setValue("saveObjWithMap", saveObjWithMapCheck->isChecked());
        event->accept();
        emit documentClosed();
        return;
    }

    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
    {
        event->ignore();
        return;
    }

    // Also release redo-only MPC payloads left after saving an undone state.
    // They no longer have a reachable command once the window closes.
    discardPendingMpcResources();
    removeWindowFromSharedPendingMpcRegistry(this);

    QSettings settings("JXQY", "MapEditor");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("maximized", isMaximized());
    settings.setValue("saveNpcWithMap", saveNpcWithMapCheck->isChecked());
    settings.setValue("saveObjWithMap", saveObjWithMapCheck->isChecked());

    event->accept();
    emit documentClosed();
}

void MapEditorWindow::onCloseRequested()
{
    close();
}

void MapEditorWindow::setupMenuBar()
{
    // MapEditorWindow 是 QWidget，需要手动创建 QMenuBar 并插入到 mainLayout 顶部。
    QMenuBar* menuBar = new QMenuBar(this);
    menuBar->setObjectName(QStringLiteral("mapMenuBar"));
    ui->mainLayout->insertWidget(0, menuBar);

    // 文件菜单
    mapFileMenu = menuBar->addMenu(tr("文件"));
    mapFileMenu->setObjectName(QStringLiteral("mapFileMenu"));
    mapFileMenu->addAction(ui->actionNewMap);
    mapFileMenu->addAction(ui->actionOpenMap);
    mapFileMenu->addAction(ui->actionSaveMap);
    mapFileMenu->addAction(ui->actionSaveMapAs);
    mapFileMenu->addSeparator();
    mapFileMenu->addAction(ui->actionExportThumbnail);
    mapFileMenu->addAction(ui->actionExportBmp);

    // 编辑菜单（承接原画布右键菜单项）
    mapEditMenu = menuBar->addMenu(tr("编辑"));
    mapEditMenu->setObjectName(QStringLiteral("mapEditMenu"));
    mapEditMenu->addAction(ui->actionUndo);
    mapEditMenu->addAction(ui->actionRedo);
    mapEditMenu->addSeparator();
    // 复制/粘贴区域
    actionCopySelectedArea = new QAction(tr("复制选中区域"), this);
    actionCopySelectedArea->setObjectName(QStringLiteral("actionCopySelectedArea"));
    actionCopySelectedArea->setShortcut(QKeySequence::Copy);
    connect(actionCopySelectedArea, &QAction::triggered, this, [this]() {
        if (canvas->getAreaStartX() >= 0 && canvas->getAreaStartY() >= 0 &&
            canvas->getAreaEndX() >= 0 && canvas->getAreaEndY() >= 0)
        {
            onCopyArea();
        }
        else if (statusToolLabel)
        {
            statusToolLabel->setText(tr("请先用区域选择工具框选区域再复制"));
        }
    });
    mapEditMenu->addAction(actionCopySelectedArea);

    actionPasteArea = new QAction(tr("粘贴区域"), this);
    actionPasteArea->setObjectName(QStringLiteral("actionPasteArea"));
    actionPasteArea->setShortcut(QKeySequence::Paste);
    connect(actionPasteArea, &QAction::triggered, this, [this]() {
        if (canvas->hasClipboardData())
        {
            // 粘贴到当前 hover 瓦片或选中瓦片
            int tx = canvas->getHoverTileX();
            int ty = canvas->getHoverTileY();
            if (tx < 0 || ty < 0)
            {
                tx = canvas->getSelectedTileX();
                ty = canvas->getSelectedTileY();
            }
            if (tx >= 0 && ty >= 0)
                canvas->pasteArea(tx, ty);
            else if (statusToolLabel)
                statusToolLabel->setText(tr("请将鼠标移到目标瓦片再粘贴"));
        }
        else if (statusToolLabel)
        {
            statusToolLabel->setText(tr("剪贴板为空，无内容可粘贴"));
        }
    });
    mapEditMenu->addAction(actionPasteArea);

    actionClearClipboard = new QAction(tr("清空剪贴板"), this);
    actionClearClipboard->setObjectName(QStringLiteral("actionClearClipboard"));
    connect(actionClearClipboard, &QAction::triggered, this, [this]() {
        canvas->clearClipboard();
        if (statusToolLabel)
            statusToolLabel->setText(tr("剪贴板已清空"));
    });
    mapEditMenu->addAction(actionClearClipboard);

    mapEditMenu->addSeparator();

    actionFillSelectedArea = new QAction(tr("填充当前选区"), this);
    actionFillSelectedArea->setObjectName("actionFillSelectedArea");
    actionFillSelectedArea->setToolTip(tr(
        "用当前画笔填充区域选区。单图层写当前绘制层；全部图层按画笔写回三层"));
    connect(actionFillSelectedArea, &QAction::triggered,
            this, &MapEditorWindow::onFillSelectedArea);
    mapEditMenu->addAction(actionFillSelectedArea);

    actionClearSelectedArea = new QAction(tr("清除当前选区"), this);
    actionClearSelectedArea->setObjectName("actionClearSelectedArea");
    actionClearSelectedArea->setToolTip(tr(
        "清除区域选区内的 tile。单图层清当前绘制层；全部图层清三层，保留障碍/陷阱"));
    connect(actionClearSelectedArea, &QAction::triggered,
            this, &MapEditorWindow::onClearSelectedArea);
    mapEditMenu->addAction(actionClearSelectedArea);

    mapEditMenu->addSeparator();

    actionClearAllObstacles = new QAction(tr("清除所有障碍"), this);
    actionClearAllObstacles->setObjectName(QStringLiteral("actionClearAllObstacles"));
    connect(actionClearAllObstacles, &QAction::triggered, this, &MapEditorWindow::onClearAllObstacles);
    mapEditMenu->addAction(actionClearAllObstacles);

    actionClearAllTraps = new QAction(tr("清除所有陷阱"), this);
    actionClearAllTraps->setObjectName(QStringLiteral("actionClearAllTraps"));
    connect(actionClearAllTraps, &QAction::triggered, this, &MapEditorWindow::onClearAllTraps);
    mapEditMenu->addAction(actionClearAllTraps);

    mapEditMenu->addSeparator();

    actionEditMpcInfo = new QAction(tr("编辑MPC信息..."), this);
    actionEditMpcInfo->setObjectName(QStringLiteral("actionEditMpcInfo"));
    connect(actionEditMpcInfo, &QAction::triggered, this, &MapEditorWindow::onEditMpcInfo);
    mapEditMenu->addAction(actionEditMpcInfo);

    actionEditTrapScripts = new QAction(tr("编辑陷阱脚本..."), this);
    actionEditTrapScripts->setObjectName("actionEditTrapScripts");
    connect(actionEditTrapScripts, &QAction::triggered, this, &MapEditorWindow::onEditTrapScripts);
    mapEditMenu->addAction(actionEditTrapScripts);

    actionResizeMap = new QAction(tr("调整地图尺寸..."), this);
    actionResizeMap->setObjectName(QStringLiteral("actionResizeMap"));
    connect(actionResizeMap, &QAction::triggered, this, &MapEditorWindow::onResizeMap);
    mapEditMenu->addAction(actionResizeMap);

    // 视图菜单
    mapViewMenu = menuBar->addMenu(tr("视图"));
    mapViewMenu->setObjectName(QStringLiteral("mapViewMenu"));
    mapViewMenu->addAction(ui->actionZoomIn);
    mapViewMenu->addAction(ui->actionZoomOut);
    mapViewMenu->addAction(ui->actionResetZoom);
    mapViewMenu->addAction(ui->actionZoomToFit);
    mapViewMenu->addSeparator();
    mapViewMenu->addAction(ui->actionGotoTile);
    mapViewMenu->addAction(ui->actionMapStatistics);

    // 工具菜单
    mapToolMenu = menuBar->addMenu(tr("工具"));
    mapToolMenu->setObjectName(QStringLiteral("mapToolMenu"));
    mapToolMenu->addAction(ui->actionToolSelect);
    mapToolMenu->addAction(ui->actionToolPan);
    mapToolMenu->addAction(ui->actionToolTilePaint);
    mapToolMenu->addAction(ui->actionToolObstaclePaint);
    mapToolMenu->addAction(ui->actionToolTrapPaint);
    mapToolMenu->addAction(ui->actionToolNpcPlace);
    mapToolMenu->addAction(ui->actionToolObjectPlace);
    mapToolMenu->addAction(ui->actionToolAreaSelect);

    // 列表菜单
    mapListMenu = menuBar->addMenu(tr("列表"));
    mapListMenu->setObjectName(QStringLiteral("mapListMenu"));
    mapListMenu->addAction(ui->actionLoadNpcList);
    mapListMenu->addAction(ui->actionLoadObjectList);
    mapListMenu->addSeparator();

    actionSaveNpcList = new QAction(tr("保存NPC列表"), this);
    actionSaveNpcList->setObjectName(QStringLiteral("actionSaveNpcList"));
    connect(actionSaveNpcList, &QAction::triggered, this, &MapEditorWindow::onSaveNpcList);
    mapListMenu->addAction(actionSaveNpcList);

    actionSaveNpcListAs = new QAction(tr("NPC列表另存为..."), this);
    actionSaveNpcListAs->setObjectName(QStringLiteral("actionSaveNpcListAs"));
    connect(actionSaveNpcListAs, &QAction::triggered, this, &MapEditorWindow::onSaveNpcListAs);
    mapListMenu->addAction(actionSaveNpcListAs);

    actionCloseNpcList = new QAction(tr("关闭NPC列表"), this);
    actionCloseNpcList->setObjectName(QStringLiteral("actionCloseNpcList"));
    connect(actionCloseNpcList, &QAction::triggered, this, &MapEditorWindow::onCloseNpcList);
    mapListMenu->addAction(actionCloseNpcList);

    mapListMenu->addSeparator();

    actionSaveObjectList = new QAction(tr("保存物体列表"), this);
    actionSaveObjectList->setObjectName(QStringLiteral("actionSaveObjectList"));
    connect(actionSaveObjectList, &QAction::triggered, this, &MapEditorWindow::onSaveObjectList);
    mapListMenu->addAction(actionSaveObjectList);

    actionSaveObjectListAs = new QAction(tr("物体列表另存为..."), this);
    actionSaveObjectListAs->setObjectName(QStringLiteral("actionSaveObjectListAs"));
    connect(actionSaveObjectListAs, &QAction::triggered, this, &MapEditorWindow::onSaveObjectListAs);
    mapListMenu->addAction(actionSaveObjectListAs);

    actionCloseObjectList = new QAction(tr("关闭物体列表"), this);
    actionCloseObjectList->setObjectName(QStringLiteral("actionCloseObjectList"));
    connect(actionCloseObjectList, &QAction::triggered, this, &MapEditorWindow::onCloseObjectList);
    mapListMenu->addAction(actionCloseObjectList);
}

void MapEditorWindow::retranslateDynamicUi()
{
    if (taskPanel)
    {
        taskPanel->setTabText(taskPanel->indexOf(paintTaskPage), tr("绘制"));
        taskPanel->setTabText(taskPanel->indexOf(entityTaskPage), tr("NPC / 物体"));
        taskPanel->setTabText(taskPanel->indexOf(inspectTaskPage), tr("查看"));
    }
    if (tileInfoTree)
        tileInfoTree->setHeaderLabels({tr("属性"), tr("值")});
    if (entityInfoTree)
        entityInfoTree->setHeaderLabels({tr("属性"), tr("值")});

    // 顶部动态菜单标题
    if (mapFileMenu) mapFileMenu->setTitle(tr("文件"));
    if (mapEditMenu) mapEditMenu->setTitle(tr("编辑"));
    if (mapViewMenu) mapViewMenu->setTitle(tr("视图"));
    if (mapToolMenu) mapToolMenu->setTitle(tr("工具"));
    if (mapListMenu) mapListMenu->setTitle(tr("列表"));

    // 编辑菜单内动态动作
    if (actionCopySelectedArea) actionCopySelectedArea->setText(tr("复制选中区域"));
    if (actionPasteArea) actionPasteArea->setText(tr("粘贴区域"));
    if (actionClearClipboard) actionClearClipboard->setText(tr("清空剪贴板"));
    if (actionFillSelectedArea)
    {
        actionFillSelectedArea->setText(tr("填充当前选区"));
        actionFillSelectedArea->setToolTip(tr(
            "用当前画笔填充区域选区。单图层写当前绘制层；全部图层按画笔写回三层"));
    }
    if (actionClearSelectedArea)
    {
        actionClearSelectedArea->setText(tr("清除当前选区"));
        actionClearSelectedArea->setToolTip(tr(
            "清除区域选区内的 tile。单图层清当前绘制层；全部图层清三层，保留障碍/陷阱"));
    }
    if (actionClearAllObstacles) actionClearAllObstacles->setText(tr("清除所有障碍"));
    if (actionClearAllTraps) actionClearAllTraps->setText(tr("清除所有陷阱"));
    if (actionEditMpcInfo) actionEditMpcInfo->setText(tr("编辑MPC信息..."));
    if (actionEditTrapScripts) actionEditTrapScripts->setText(tr("编辑陷阱脚本..."));
    if (actionResizeMap) actionResizeMap->setText(tr("调整地图尺寸..."));

    // 列表菜单内动态动作
    if (actionSaveNpcList) actionSaveNpcList->setText(tr("保存NPC列表"));
    if (actionSaveNpcListAs) actionSaveNpcListAs->setText(tr("NPC列表另存为..."));
    if (actionCloseNpcList) actionCloseNpcList->setText(tr("关闭NPC列表"));
    if (actionSaveObjectList) actionSaveObjectList->setText(tr("保存物体列表"));
    if (actionSaveObjectListAs) actionSaveObjectListAs->setText(tr("物体列表另存为..."));
    if (actionCloseObjectList) actionCloseObjectList->setText(tr("关闭物体列表"));

    // 状态栏当前工具名按当前工具重新翻译（避免切语言后停留在旧语言）。
    if (canvas && statusToolLabel)
        syncToolActionFromCanvas(canvas->getEditTool());

    // 实体标签页与文件路径标签随语言刷新（含“未加载/新建”等状态）。
    updateEntityTabs();
}

void MapEditorWindow::setupToolbar()
{
    connect(ui->actionNewMap, &QAction::triggered, this, &MapEditorWindow::onNewMap);
    connect(ui->actionOpenMap, &QAction::triggered, this, &MapEditorWindow::onOpenMap);
    connect(ui->actionSaveMap, &QAction::triggered, this, &MapEditorWindow::onSaveMap);
    connect(ui->actionSaveMapAs, &QAction::triggered, this, &MapEditorWindow::onSaveMapAs);
    connect(ui->actionExportThumbnail, &QAction::triggered, this, &MapEditorWindow::onExportThumbnail);
    connect(ui->actionExportBmp, &QAction::triggered, this, &MapEditorWindow::onExportBmp);

    actionUndo = ui->actionUndo;
    actionRedo = ui->actionRedo;
    connect(ui->actionUndo, &QAction::triggered, this, &MapEditorWindow::onUndo);
    connect(ui->actionRedo, &QAction::triggered, this, &MapEditorWindow::onRedo);

    connect(ui->actionLoadNpcList, &QAction::triggered, this, [this]() {
        QString filter = tr("NPC列表文件 (*.ini *.npc);;所有文件 (*.*)");
        QString fileName = QFileDialog::getOpenFileName(
            this,
            tr("加载NPC列表"),
            getDefaultDirectoryForList(true),
            filter,
            nullptr,
            QFileDialog::DontResolveSymlinks);
        if (!fileName.isEmpty())
            loadNpcListFromFile(fileName);
    });

    connect(ui->actionLoadObjectList, &QAction::triggered, this, [this]() {
        QString filter = tr("物体列表文件 (*.ini *.obj);;所有文件 (*.*)");
        QString fileName = QFileDialog::getOpenFileName(
            this,
            tr("加载物体列表"),
            getDefaultDirectoryForList(false),
            filter,
            nullptr,
            QFileDialog::DontResolveSymlinks);
        if (!fileName.isEmpty())
            loadObjectListFromFile(fileName);
    });

    connect(ui->actionToolSelect, &QAction::triggered, this, &MapEditorWindow::onToolSelect);
    connect(ui->actionToolTilePaint, &QAction::triggered, this, &MapEditorWindow::onToolTilePaint);
    connect(ui->actionToolObstaclePaint, &QAction::triggered, this, &MapEditorWindow::onToolObstaclePaint);
    connect(ui->actionToolTrapPaint, &QAction::triggered, this, &MapEditorWindow::onToolTrapPaint);
    connect(ui->actionToolNpcPlace, &QAction::triggered, this, &MapEditorWindow::onToolNpcPlace);
    connect(ui->actionToolObjectPlace, &QAction::triggered, this, &MapEditorWindow::onToolObjectPlace);
    connect(ui->actionToolTilePicker, &QAction::triggered, this, [this]() {
        canvas->setEditTool(MapEditTool::TilePicker);
        if (statusToolLabel) statusToolLabel->setText(tr("吸管工具"));
    });
    connect(ui->actionToolAreaSelect, &QAction::triggered, this, [this]() {
        canvas->setEditTool(MapEditTool::AreaSelect);
        if (statusToolLabel) statusToolLabel->setText(tr("区域选择"));
    });
    connect(ui->actionToolPan, &QAction::triggered, this, [this]() {
        canvas->setEditTool(MapEditTool::Pan);
        if (statusToolLabel) statusToolLabel->setText(tr("拖拽地图"));
    });

    connect(ui->actionZoomIn, &QAction::triggered, this, [this]() { canvas->zoomIn(); });
    connect(ui->actionZoomOut, &QAction::triggered, this, [this]() { canvas->zoomOut(); });
    connect(ui->actionResetZoom, &QAction::triggered, this, [this]() { canvas->resetZoom(); });
    connect(ui->actionGotoTile, &QAction::triggered, this, &MapEditorWindow::onGotoTile);
    connect(ui->actionZoomToFit, &QAction::triggered, this, &MapEditorWindow::onZoomToFit);
    connect(ui->actionMapStatistics, &QAction::triggered, this, &MapEditorWindow::onMapStatistics);

    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->addAction(ui->actionToolSelect);
    toolGroup->addAction(ui->actionToolTilePaint);
    toolGroup->addAction(ui->actionToolObstaclePaint);
    toolGroup->addAction(ui->actionToolTrapPaint);
    toolGroup->addAction(ui->actionToolNpcPlace);
    toolGroup->addAction(ui->actionToolObjectPlace);
    toolGroup->addAction(ui->actionToolTilePicker);
    toolGroup->addAction(ui->actionToolAreaSelect);
    toolGroup->addAction(ui->actionToolPan);

    // Remove edit tool actions from the top toolbar;
    // they live in the floating toolbar on the canvas instead.
    ui->mapToolBar->removeAction(ui->actionToolSelect);
    ui->mapToolBar->removeAction(ui->actionToolTilePaint);
    ui->mapToolBar->removeAction(ui->actionToolObstaclePaint);
    ui->mapToolBar->removeAction(ui->actionToolTrapPaint);
    ui->mapToolBar->removeAction(ui->actionToolNpcPlace);
    ui->mapToolBar->removeAction(ui->actionToolObjectPlace);
    ui->mapToolBar->removeAction(ui->actionToolTilePicker);
    ui->mapToolBar->removeAction(ui->actionToolAreaSelect);
    ui->mapToolBar->removeAction(ui->actionToolPan);
    ui->mapToolBar->removeAction(ui->actionLoadNpcList);
    ui->mapToolBar->removeAction(ui->actionLoadObjectList);

    // 隐藏窗口上方第二行 QToolBar：菜单栏（第一行）已承接所有命令、快捷键和 QAction，
    // 画布左上角的浮动工具条继续保留。不删除 QAction，只移除多余的工具栏展示。
    ui->mapToolBar->setVisible(false);
}

void MapEditorWindow::setupConnections()
{
    connect(canvas, &MapRenderCanvas::tileClicked, this, &MapEditorWindow::onTileClicked);
    connect(canvas, &MapRenderCanvas::tileHovered, this, &MapEditorWindow::onTileHovered);
    connect(canvas, &MapRenderCanvas::entitySelected, this, &MapEditorWindow::onEntitySelected);
    connect(canvas, &MapRenderCanvas::entityDoubleClicked, this, &MapEditorWindow::onEntityDoubleClicked);
    connect(canvas, &MapRenderCanvas::entityMoved, this, &MapEditorWindow::onEntityMoved);
    connect(canvas, &MapRenderCanvas::entityListChanged, this, &MapEditorWindow::onEntityListChanged);
    connect(canvas, &MapRenderCanvas::tileEdited, this, &MapEditorWindow::onTileEdited);
    connect(canvas, &MapRenderCanvas::tileAboutToBeEdited, this, &MapEditorWindow::onTileAboutToBeEdited);
    connect(canvas, &MapRenderCanvas::entityPlaced, this, &MapEditorWindow::onEntityPlaced);
    connect(canvas, &MapRenderCanvas::entityMoveStarted, this, &MapEditorWindow::onEntityMoveStarted);
    connect(canvas, &MapRenderCanvas::entityDeleteRequested, this, &MapEditorWindow::onEntityDeleteRequested);
    connect(canvas, &MapRenderCanvas::zoomChanged, this, &MapEditorWindow::onZoomChanged);
    connect(canvas, &MapRenderCanvas::tilePicked, this, [this](int mpcIndex, int frameIndex, int layer) {
        syncPaintUIFromPick(mpcIndex, frameIndex, layer);
        // 仅在吸管工具下自动切回瓦片绘制；Select/TilePaint 下拾取保持当前工具
        if (canvas->getEditTool() == MapEditTool::TilePicker)
        {
            canvas->setEditTool(MapEditTool::TilePaint);
            ui->actionToolTilePaint->setChecked(true);
            if (statusToolLabel)
                statusToolLabel->setText(tr("瓦片绘制"));
        }
        if (mpcIndex == 0 && statusToolLabel)
            statusToolLabel->setText(tr("已拾取空 tile（画笔切到空图层）"));
    });
    connect(canvas, &MapRenderCanvas::tilePickedAllLayers, this, [this](const MapTileData& tileData, int, int) {
        // 仅在吸管工具下自动切回瓦片绘制；Select/TilePaint 下拾取保持当前工具。
        // 注意：setEditTool 会触发 editToolChanged -> syncToolActionFromCanvas 覆盖
        // 状态栏，因此先切工具，再调用 syncPaintUIFromAllLayersPick 设置
        // “已拾取全部图层：...”提示，保证多层拾取信息不被“瓦片绘制”覆盖。
        if (canvas->getEditTool() == MapEditTool::TilePicker)
        {
            canvas->setEditTool(MapEditTool::TilePaint);
            ui->actionToolTilePaint->setChecked(true);
        }
        syncPaintUIFromAllLayersPick(tileData);
    });
    connect(canvas, &MapRenderCanvas::obstaclePicked, this, [this](uint8_t obstacle, int, int) {
        // 拾取障碍后同步左侧下拉框与画笔值
        int comboIndex = obstacleComboBox->findData(obstacle);
        if (comboIndex < 0)
        {
            obstacleComboBox->addItem(
                tr("自定义 (0x%1)").arg(obstacle, 2, 16, QChar('0')).toUpper(),
                static_cast<int>(obstacle));
            comboIndex = obstacleComboBox->count() - 1;
        }
        obstacleComboBox->setCurrentIndex(comboIndex);
        canvas->setPaintObstacle(obstacle);
        if (statusToolLabel)
        {
            QString obstacleName;
            if (obstacle == 0x00) obstacleName = tr("可通过");
            else if (obstacle == 0x40) obstacleName = tr("透明");
            else if (obstacle == 0x60) obstacleName = tr("跳透");
            else if (obstacle == 0x80) obstacleName = tr("实体");
            else if (obstacle == 0xA0) obstacleName = tr("跳障");
            else obstacleName = QString("0x%1").arg(obstacle, 2, 16, QChar('0')).toUpper();
            statusToolLabel->setText(tr("已拾取障碍 [%1]").arg(obstacleName));
        }
    });
    connect(canvas, &MapRenderCanvas::trapPicked, this, [this](uint8_t trapIndex, int, int) {
        // 拾取陷阱后同步左侧输入框与画笔值
        trapIndexSpinBox->setValue(trapIndex);
        canvas->setPaintTrapIndex(trapIndex);
        if (statusToolLabel)
        {
            if (trapIndex == 0)
                statusToolLabel->setText(tr("已拾取陷阱 [无（清除）]"));
            else
                statusToolLabel->setText(tr("已拾取陷阱 [索引=%1]").arg(trapIndex));
        }
    });
    connect(canvas, &MapRenderCanvas::areaCopied, this, [this](int width, int height) {
        if (statusToolLabel)
            statusToolLabel->setText(tr("已复制 %1x%2 区域").arg(width).arg(height));
    });
    connect(canvas, &MapRenderCanvas::areaPasted, this, [this](int tileX, int tileY, int width, int height) {
        Q_UNUSED(tileX);
        Q_UNUSED(tileY);
        const auto& oldTiles = canvas->getPasteOldTiles();
        const auto& newTiles = canvas->getPasteNewTiles();
        if (!oldTiles.empty())
        {
            undoRedoManager.pushCommand(new TileFillCommand(oldTiles, newTiles, &mapEditor));
            syncDirtyStateFromUndoHistory();
        }
        minimapWidget->refreshMinimap();
        if (statusToolLabel)
            statusToolLabel->setText(tr("已粘贴 %1x%2 区域").arg(width).arg(height));
    });
    connect(canvas, &MapRenderCanvas::entityDuplicateRequested, this, &MapEditorWindow::onDuplicateEntity);
    connect(canvas, &MapRenderCanvas::selectAllRequested, this, &MapEditorWindow::onSelectAll);
    connect(canvas, &MapRenderCanvas::selectionCleared, this, [this]() {
        clearRightInfoPanel();
        // 取消选中后立即用当前悬停瓦片刷新右侧面板
        int hx = canvas->getHoverTileX();
        int hy = canvas->getHoverTileY();
        if (hx >= 0 && hy >= 0)
        {
            updateTileInfoPanel(hx, hy, false);
        }
    });

    // 工具切换时同步 QAction 选中态和状态栏
    connect(canvas, &MapRenderCanvas::editToolChanged, this, &MapEditorWindow::syncToolActionFromCanvas);

    // 拖拽/缩放/resize 时实时刷新小地图视口框
    connect(canvas, &MapRenderCanvas::viewportChanged, this, [this]() {
        if (minimapWidget)
            minimapWidget->update();
    });
}

bool MapEditorWindow::setAssetsBasePath(const QString& path)
{
    const Decision decision = prepareAssetsPathSwitch(path);
    if (decision == Decision::Cancelled || !resolveAssetsPathSwitch(decision))
        return false;
    commitAssetsPathSwitch(path);
    return true;
}

AssetsPathSwitchParticipant::Decision MapEditorWindow::prepareAssetsPathSwitch(
    const QString& path) const
{
    const QString currentRoot = assetsBasePath.isEmpty()
        ? QString() : normalizedAbsolutePath(assetsBasePath);
    const QString requestedRoot = path.isEmpty()
        ? QString() : normalizedAbsolutePath(path);
    if (!pendingCreatedMpcResources.empty() &&
        currentRoot.compare(requestedRoot, managedPathCaseSensitivity()) != 0)
    {
        QMessageBox::warning(
            const_cast<MapEditorWindow*>(this), tr("资源目录未切换"),
            tr("当前地图仍有可撤销的未保存 MPC 资源，切换 assets 根目录会使其引用失效。\n\n"
               "请先保存并关闭当前地图，或放弃这些 MPC 修改后再切换资源目录。"));
        return Decision::Cancelled;
    }

    return Decision::Ready;
}

bool MapEditorWindow::resolveAssetsPathSwitch(Decision decision)
{
    return decision != Decision::Cancelled;
}

void MapEditorWindow::commitAssetsPathSwitch(const QString& path)
{
    assetsBasePath = path;
    mpcCache.setAssetsBasePath(path.toUtf8().toStdString());
    if (canvas)
    {
        canvas->invalidateRenderRangeCache();
        canvas->update();
    }
    if (minimapWidget)
        minimapWidget->refreshMinimap();
}

QString MapEditorWindow::currentAssetsPath() const
{
    return assetsBasePath;
}

QString MapEditorWindow::getAssetsBasePath() const
{
    return assetsBasePath;
}

QList<ProjectDocumentState> MapEditorWindow::currentProjectDocuments() const
{
    QList<ProjectDocumentState> documents;
    if (!currentMapFileName.isEmpty())
    {
        documents.append(
            {currentMapFileName, ProjectDocumentType::Map, isModified});
    }
    if (isNpcListOpen && !currentNpcFileName.isEmpty())
    {
        documents.append(
            {currentNpcFileName, ProjectDocumentType::NpcList,
             isNpcListModified});
    }
    if (isObjectListOpen && !currentObjectFileName.isEmpty())
    {
        documents.append(
            {currentObjectFileName, ProjectDocumentType::ObjectList,
             isObjectListModified});
    }
    return documents;
}

QList<DesktopRunDocumentSnapshot>
MapEditorWindow::captureDesktopRunMapDocuments(
    bool includeCleanDirectDocuments,
    bool pinRuntimeSaveCompanions) const
{
    QList<DesktopRunDocumentSnapshot> documents;
    const QString mapFilePath = currentMapFileName.isEmpty()
        ? QString() : normalizedAbsolutePath(currentMapFileName);
    const QString npcFilePath = currentNpcFileName.isEmpty()
        ? QString() : normalizedAbsolutePath(currentNpcFileName);
    const QString objectFilePath = currentObjectFileName.isEmpty()
        ? QString() : normalizedAbsolutePath(currentObjectFileName);

    if (mapEditor.isLoaded() &&
        (includeCleanDirectDocuments || isModified))
    {
        DesktopRunDocumentSnapshot mapSnapshot;
        mapSnapshot.filePath = mapFilePath;
        mapSnapshot.type = ProjectDocumentType::Map;
        mapSnapshot.dirty = isModified;
        const std::vector<uint8_t> bytes = mapEditor.saveToBuffer();
        if (!bytes.empty())
        {
            mapSnapshot.serializationSupported = true;
            mapSnapshot.bytes = QByteArray(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<qsizetype>(bytes.size()));
        }
        else
        {
            mapSnapshot.diagnosticCode =
                QStringLiteral("editor_run.overlay.map_snapshot_failed");
        }
        documents.append(std::move(mapSnapshot));
    }

    if (isNpcListOpen &&
        (includeCleanDirectDocuments ||
         isNpcListModified))
    {
        DesktopRunDocumentSnapshot npcSnapshot;
        npcSnapshot.filePath = npcFilePath;
        npcSnapshot.type = ProjectDocumentType::NpcList;
        npcSnapshot.dirty = isNpcListModified;
        npcSnapshot.includeInOverlay =
            pinRuntimeSaveCompanions &&
            npcListLoadedFromRuntimeSave;
        const std::string bytes = canvas->serializeNpcList();
        npcSnapshot.serializationSupported = true;
        npcSnapshot.bytes = QByteArray(
            bytes.data(), static_cast<qsizetype>(bytes.size()));
        documents.append(std::move(npcSnapshot));
    }

    if (isObjectListOpen &&
        (includeCleanDirectDocuments ||
         isObjectListModified))
    {
        DesktopRunDocumentSnapshot objectSnapshot;
        objectSnapshot.filePath = objectFilePath;
        objectSnapshot.type = ProjectDocumentType::ObjectList;
        objectSnapshot.dirty = isObjectListModified;
        objectSnapshot.includeInOverlay =
            pinRuntimeSaveCompanions &&
            objectListLoadedFromRuntimeSave;
        const std::string bytes = canvas->serializeObjectList();
        objectSnapshot.serializationSupported = true;
        objectSnapshot.bytes = QByteArray(
            bytes.data(), static_cast<qsizetype>(bytes.size()));
        documents.append(std::move(objectSnapshot));
    }

    std::set<QString, ManagedPathLess> referencedPaths;
    for (int slot = 0; slot < MAP_EDITOR_MPC_COUNT; ++slot)
    {
        const MpcInfoData& info = mapEditor.getMpcInfo(slot);
        if (info.name.empty())
            continue;
        const QString path = resolveManagedMpcTargetPath(
            QString::fromUtf8(info.name.c_str()));
        if (!path.isEmpty())
            referencedPaths.insert(path);
    }

    std::map<QString, QByteArray, ManagedPathLess> pendingResources;
    for (const auto& entry : pendingCreatedMpcResources)
    {
        const QString path = normalizedAbsolutePath(entry.first);
        if (referencedPaths.count(path) != 0)
            pendingResources[path] = entry.second;
    }
    for (const QString& path : referencedPaths)
    {
        const SharedPendingMpcResource* shared =
            findSharedPendingMpcResource(path);
        if (shared && !shared->durable)
            pendingResources[normalizedAbsolutePath(path)] = shared->bytes;
    }

    for (const auto& entry : pendingResources)
    {
        DesktopRunDocumentSnapshot resourceSnapshot;
        resourceSnapshot.filePath = entry.first;
        resourceSnapshot.ownerMapFilePath =
            mapFilePath;
        resourceSnapshot.type = ProjectDocumentType::Image;
        resourceSnapshot.dirty = true;
        resourceSnapshot.includeInOverlay = true;
        resourceSnapshot.serializationSupported = true;
        resourceSnapshot.bytes = entry.second;
        documents.append(std::move(resourceSnapshot));
    }

    return documents;
}

MapEditorWindow::DesktopRunSnapshotBundle
MapEditorWindow::desktopRunSnapshotBundle() const
{
    DesktopRunSnapshotBundle bundle;
    bundle.mapLoaded = mapEditor.isLoaded();
    bundle.assetsBasePath = assetsBasePath.isEmpty()
        ? QString() : normalizedAbsolutePath(assetsBasePath);
    bundle.currentMapFilePath = currentMapFileName.isEmpty()
        ? QString() : normalizedAbsolutePath(currentMapFileName);
    bundle.currentNpcFilePath = currentNpcFileName.isEmpty()
        ? QString() : normalizedAbsolutePath(currentNpcFileName);
    bundle.currentObjectFilePath = currentObjectFileName.isEmpty()
        ? QString() : normalizedAbsolutePath(currentObjectFileName);
    bundle.npcListOpen = isNpcListOpen;
    bundle.objectListOpen = isObjectListOpen;
    bundle.mapWidth = mapEditor.getWidth();
    bundle.mapHeight = mapEditor.getHeight();
    bundle.documents =
        captureDesktopRunMapDocuments(true, true);
    return bundle;
}

QList<DesktopRunDocumentSnapshot>
MapEditorWindow::desktopRunGenericDocumentSnapshots() const
{
    return genericDesktopRunMapDocumentSnapshots(
        captureDesktopRunMapDocuments(false, false));
}

bool MapEditorWindow::canAdoptDocumentPath(
    const QString& currentPath, const QString& targetPath) const
{
    return !documentPathValidator ||
        documentPathValidator(currentPath, targetPath);
}

bool MapEditorWindow::hasDocumentTargetConflict(
    ProjectDocumentType documentType, const QString& targetPath) const
{
    if (targetPath.trimmed().isEmpty())
        return true;

    const QString normalizedTarget = normalizedAbsolutePath(targetPath);
    auto matches = [&normalizedTarget](const QString& candidate)
    {
        return !candidate.isEmpty() &&
            normalizedAbsolutePath(candidate).compare(
                normalizedTarget, managedPathCaseSensitivity()) == 0;
    };

    if (documentType != ProjectDocumentType::Map &&
        matches(currentMapFileName))
    {
        return true;
    }
    if (documentType != ProjectDocumentType::NpcList &&
        isNpcListOpen && matches(currentNpcFileName))
    {
        return true;
    }
    if (documentType != ProjectDocumentType::ObjectList &&
        isObjectListOpen && matches(currentObjectFileName))
    {
        return true;
    }
    return false;
}

bool MapEditorWindow::openMapFile(const QString& fileName)
{
    if (fileName.trimmed().isEmpty() ||
        !canAdoptDocumentPath(currentMapFileName, fileName))
    {
        return false;
    }

    if (!mapEditor.loadFromFile(fileName.toUtf8().toStdString()))
    {
        QMessageBox::warning(this, tr("错误"),
            tr("无法加载地图文件: %1\n%2")
                .arg(fileName)
                .arg(QString::fromUtf8(mapEditor.getLastError())));
        return false;
    }

    // Loading succeeded transactionally, so the previous document and its
    // redo-only resource payload can now be retired safely.
    discardPendingMpcResources();

    currentMapFileName = fileName;
    isModified = false;

    mapEditor.setMapFileName(fileName.toUtf8().toStdString());

    mpcCache.setAssetsBasePath(assetsBasePath.toUtf8().toStdString());
    mpcCache.clearCache();

    updateMpcComboBox();
    updateWindowTitle();

    canvas->setMapFileEditor(&mapEditor);
    canvas->setMpcImageCache(&mpcCache);

    if (mapEditor.isLoaded())
    {
        canvas->centerOnTile(mapEditor.getWidth() / 2, mapEditor.getHeight() / 2);
        if (statusMapSizeLabel)
        {
            statusMapSizeLabel->setText(
            tr("地图: %1x%2").arg(mapEditor.getWidth()).arg(mapEditor.getHeight()));
        }
    }

    canvas->clearEntities();
    canvas->clearPlacingEntity();
    canvas->clearSelection();
    canvas->clearTransientEditState();
    currentNpcFileName.clear();
    currentObjectFileName.clear();
    isNpcListOpen = false;
    isObjectListOpen = false;
    npcListLoadedFromRuntimeSave = false;
    objectListLoadedFromRuntimeSave = false;
    isNpcListModified = false;
    isObjectListModified = false;
    updateNpcListWidget();
    updateObjectListWidget();
    updateEntityTabs();
    clearRightInfoPanel();

    undoRedoManager.clear();
    undoRedoManager.markSaved(UndoDomain::All);

    pendingMoveEntityIndex = -1;
    pendingMoveEntityIsNpc = true;
    pendingMoveOldMapX = 0;
    pendingMoveOldMapY = 0;
    hasPendingOldTileData = false;

    minimapWidget->setMapFileEditor(&mapEditor);
    minimapWidget->refreshMinimap();

    QFileInfo mapInfo(fileName);
    QString baseDir = mapInfo.absolutePath();
    QString baseName = mapInfo.completeBaseName();

    bool npcListLoaded = false;
    bool npcListBlockedByOwner = false;
    auto tryLoadNpcList = [this, &npcListBlockedByOwner](
                              const QString& candidatePath)
    {
        if (!QFileInfo::exists(candidatePath))
            return false;
        if (!canAdoptDocumentPath(currentNpcFileName, candidatePath))
        {
            npcListBlockedByOwner = true;
            return false;
        }
        return loadNpcListFromFile(candidatePath, false);
    };
    if (!assetsBasePath.isEmpty())
    {
        QString saveNpcFile = assetsBasePath + "/save/game/" + baseName + ".npc";
        npcListLoaded = tryLoadNpcList(saveNpcFile);
        if (!npcListLoaded && !npcListBlockedByOwner)
        {
            QString templateNpcFile = assetsBasePath + "/ini/save/" + baseName + ".npc";
            npcListLoaded = tryLoadNpcList(templateNpcFile);
        }
    }
    // Legacy editor sidecars are a compatibility fallback.  They must not
    // shadow save/game (the runtime's current state) or the editable ini/save
    // template merely because an old file remains beside the map.
    QString autoNpcFile = baseDir + "/" + baseName + "_npc.ini";
    if (!npcListLoaded && !npcListBlockedByOwner)
        npcListLoaded = tryLoadNpcList(autoNpcFile);

    bool objectListLoaded = false;
    bool objectListBlockedByOwner = false;
    auto tryLoadObjectList = [this, &objectListBlockedByOwner](
                                 const QString& candidatePath)
    {
        if (!QFileInfo::exists(candidatePath))
            return false;
        if (!canAdoptDocumentPath(currentObjectFileName, candidatePath))
        {
            objectListBlockedByOwner = true;
            return false;
        }
        return loadObjectListFromFile(candidatePath, false);
    };
    if (!assetsBasePath.isEmpty())
    {
        QString saveObjFile = assetsBasePath + "/save/game/" + baseName + ".obj";
        objectListLoaded = tryLoadObjectList(saveObjFile);
        if (!objectListLoaded && !objectListBlockedByOwner)
        {
            QString templateObjFile = assetsBasePath + "/ini/save/" + baseName + ".obj";
            objectListLoaded = tryLoadObjectList(templateObjFile);
        }
    }
    QString autoObjFile = baseDir + "/" + baseName + "_obj.ini";
    if (!objectListLoaded && !objectListBlockedByOwner)
        objectListLoaded = tryLoadObjectList(autoObjFile);

    updateEntityCountStatus();

    // Register references from a just-opened document as well. Otherwise a
    // second window could open a map that adopts another window's pending MPC
    // and the creator would still consider the file exclusively deletable.
    lastMpcResourceSyncSucceeded = syncPendingMpcResourcesWithTable();

    emit documentStatesChanged();
    return true;
}

bool MapEditorWindow::createNewMap(int width, int height)
{
    if (width <= 0 || height <= 0 || width > 512 || height > 512)
        return false;

    std::vector<uint8_t> buffer(MAP_EDITOR_HEAD_LEN + MAP_EDITOR_MPC_COUNT * 0x40 +
                                 (size_t)width * height * 10, 0);

    MapEditorHead header = {};
    memcpy(header.head, MAP_EDITOR_HEADSTR_V2, MAP_EDITOR_HEADSTR_LEN);
    header.dataLen = width * height * 10;
    header.width = width;
    header.height = height;
    header.infoLen = 0x40;
    header.nameLen = 0x20;

    memcpy(buffer.data(), &header, sizeof(MapEditorHead));

    if (!mapEditor.loadFromBuffer(buffer.data(), buffer.size()))
        return false;

    discardPendingMpcResources();

    currentMapFileName.clear();
    isModified = true;
    mapEditor.setMapFileName("");

    mpcCache.setAssetsBasePath(assetsBasePath.toUtf8().toStdString());
    mpcCache.clearCache();

    updateMpcComboBox();
    updateWindowTitle();

    canvas->setMapFileEditor(&mapEditor);
    canvas->setMpcImageCache(&mpcCache);
    canvas->clearEntities();
    canvas->clearPlacingEntity();
    canvas->clearSelection();
    canvas->clearTransientEditState();
    currentNpcFileName.clear();
    currentObjectFileName.clear();
    isNpcListOpen = false;
    isObjectListOpen = false;
    npcListLoadedFromRuntimeSave = false;
    objectListLoadedFromRuntimeSave = false;
    isNpcListModified = false;
    isObjectListModified = false;
    updateNpcListWidget();
    updateObjectListWidget();
    updateEntityTabs();
    clearRightInfoPanel();

    undoRedoManager.clear();
    undoRedoManager.markDirty(UndoDomain::Map);
    syncDirtyStateFromUndoHistory();

    pendingMoveEntityIndex = -1;
    pendingMoveEntityIsNpc = true;
    pendingMoveOldMapX = 0;
    pendingMoveOldMapY = 0;
    hasPendingOldTileData = false;

    if (mapEditor.isLoaded())
    {
        canvas->centerOnTile(mapEditor.getWidth() / 2, mapEditor.getHeight() / 2);
        if (statusMapSizeLabel)
        {
            statusMapSizeLabel->setText(
            tr("地图: %1x%2").arg(mapEditor.getWidth()).arg(mapEditor.getHeight()));
        }
    }

    minimapWidget->setMapFileEditor(&mapEditor);
    minimapWidget->refreshMinimap();
    lastMpcResourceSyncSucceeded = syncPendingMpcResourcesWithTable();

    emit documentStatesChanged();
    return true;
}

bool MapEditorWindow::saveMapFile(const QString& fileName)
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(fileName);
    if (!mutationLease)
        return false;

    if (fileName.trimmed().isEmpty() ||
        hasDocumentTargetConflict(ProjectDocumentType::Map, fileName) ||
        !canAdoptDocumentPath(currentMapFileName, fileName))
    {
        return false;
    }

    // 从 checkbox 同步"随地图保存"状态
    if (saveNpcWithMapCheck)
        saveNpcWithMap = saveNpcWithMapCheck->isChecked();
    if (saveObjWithMapCheck)
        saveObjWithMap = saveObjWithMapCheck->isChecked();

    QFileInfo mapInfo(fileName);
    QString baseDir = mapInfo.absolutePath();
    QString baseName = mapInfo.completeBaseName();

    QString npcFileName;
    QString objFileName;
    if (!assetsBasePath.isEmpty())
    {
        // New lists must default to a directory the runtime actually reads.
        // The exact script-selected filename can still be chosen with Save As;
        // basename is only the safe default, not an assumption that every map
        // has exactly one list.
        npcFileName = assetsBasePath + "/ini/save/" + baseName + ".npc";
        objFileName = assetsBasePath + "/ini/save/" + baseName + ".obj";
    }
    else
    {
        npcFileName = baseDir + "/" + baseName + "_npc.ini";
        objFileName = baseDir + "/" + baseName + "_obj.ini";
    }

    bool shouldSaveNpc = saveNpcWithMap && isNpcListOpen;
    bool shouldSaveObj = saveObjWithMap && isObjectListOpen;
    const bool npcUsesDefaultTarget = shouldSaveNpc &&
        (currentNpcFileName.isEmpty() || npcListLoadedFromRuntimeSave);
    const bool objectUsesDefaultTarget = shouldSaveObj &&
        (currentObjectFileName.isEmpty() || objectListLoadedFromRuntimeSave);

    if (shouldSaveNpc && !currentNpcFileName.isEmpty() &&
        !npcListLoadedFromRuntimeSave)
        npcFileName = currentNpcFileName;
    if (shouldSaveObj && !currentObjectFileName.isEmpty() &&
        !objectListLoadedFromRuntimeSave)
        objFileName = currentObjectFileName;

    std::set<QString, CaseInsensitiveQStringLess> uniqueTargets;
    auto addUniqueTarget = [&uniqueTargets](const QString& path)
    {
        return uniqueTargets.insert(QDir::cleanPath(
            QFileInfo(path).absoluteFilePath())).second;
    };
    if (!addUniqueTarget(fileName) ||
        (shouldSaveNpc && !addUniqueTarget(npcFileName)) ||
        (shouldSaveObj && !addUniqueTarget(objFileName)))
    {
        QMessageBox::warning(this, tr("保存目标冲突"),
            tr("地图、NPC 列表和物体列表不能保存到同一个文件。请先使用列表“另存为”选择不同路径。"));
        return false;
    }
    if ((shouldSaveNpc &&
            !mutationLease.addResourcePath(npcFileName)) ||
        (shouldSaveObj &&
            !mutationLease.addResourcePath(objFileName)))
    {
        QMessageBox::warning(
            this, tr("保存失败"), tr("目标资源包正在更新，请稍后重试。"));
        return false;
    }
    const QString managedMpcDirectory = resolveManagedMpcDirAbsolute();
    if (!managedMpcDirectory.isEmpty() &&
        !mutationLease.addResourcePath(managedMpcDirectory))
    {
        QMessageBox::warning(
            this, tr("保存失败"), tr("MPC 资源包正在更新，请稍后重试。"));
        return false;
    }

    if (npcUsesDefaultTarget && QFileInfo::exists(npcFileName))
    {
        if (QMessageBox::warning(this, tr("确认覆盖 NPC 模板"),
                tr("默认 NPC 模板已存在：\n%1\n\n继续会覆盖该文件。若它属于另一张同名地图，请取消并使用 NPC 列表“另存为”。")
                    .arg(npcFileName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        {
            return false;
        }
    }
    if (objectUsesDefaultTarget && QFileInfo::exists(objFileName))
    {
        if (QMessageBox::warning(this, tr("确认覆盖物体模板"),
                tr("默认物体模板已存在：\n%1\n\n继续会覆盖该文件。若它属于另一张同名地图，请取消并使用物体列表“另存为”。")
                    .arg(objFileName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        {
            return false;
        }
    }

    if (shouldSaveNpc && !QDir().mkpath(QFileInfo(npcFileName).absolutePath()))
    {
        QMessageBox::warning(this, tr("错误"),
            tr("无法创建 NPC 列表目录: %1").arg(QFileInfo(npcFileName).absolutePath()));
        return false;
    }
    if (shouldSaveObj && !QDir().mkpath(QFileInfo(objFileName).absolutePath()))
    {
        QMessageBox::warning(this, tr("错误"),
            tr("无法创建物体列表目录: %1").arg(QFileInfo(objFileName).absolutePath()));
        return false;
    }

    if (!assetsBasePath.isEmpty())
    {
        QString saveNpcPath = assetsBasePath + "/save/game/" + baseName + ".npc";
        QString saveObjPath = assetsBasePath + "/save/game/" + baseName + ".obj";

        if (shouldSaveNpc && QFileInfo::exists(saveNpcPath) &&
            QDir::cleanPath(saveNpcPath).compare(
                QDir::cleanPath(npcFileName), Qt::CaseInsensitive) != 0 &&
            npcListLoadedFromRuntimeSave)
        {
            QMessageBox::StandardButton result = QMessageBox::warning(this,
                tr("运行时存档检测"),
                tr("检测到运行时存档文件:\n%1\n\nNPC 数据将保存到运行时模板/编辑目标:\n%2\n（不会覆写当前存档；运行时仍优先读取 save/game）").arg(saveNpcPath).arg(npcFileName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (result != QMessageBox::Yes)
                return false;
        }

        if (shouldSaveObj && QFileInfo::exists(saveObjPath) &&
            QDir::cleanPath(saveObjPath).compare(
                QDir::cleanPath(objFileName), Qt::CaseInsensitive) != 0 &&
            objectListLoadedFromRuntimeSave)
        {
            QMessageBox::StandardButton result = QMessageBox::warning(this,
                tr("运行时存档检测"),
                tr("检测到运行时存档文件:\n%1\n\n物体数据将保存到运行时模板/编辑目标:\n%2\n（不会覆写当前存档；运行时仍优先读取 save/game）").arg(saveObjPath).arg(objFileName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (result != QMessageBox::Yes)
                return false;
        }
    }

    const QString transactionId = QUuid::createUuid().toString(QUuid::Id128);
    QString mapTempPath = fileName + ".tmp." + transactionId;
    QString npcTempPath = npcFileName + ".tmp." + transactionId;
    QString objTempPath = objFileName + ".tmp." + transactionId;

    QStringList transactionTargets = {fileName};
    if (shouldSaveNpc)
        transactionTargets.append(npcFileName);
    if (shouldSaveObj)
        transactionTargets.append(objFileName);
    auto targetsStayInside = [&transactionTargets](const QString& root)
    {
        for (const QString& target : transactionTargets)
        {
            if (!isPathInsideDirectory(root, target))
                return false;
        }
        return true;
    };
    QString transactionRoot;
    if (!assetsBasePath.isEmpty() &&
        targetsStayInside(assetsBasePath))
    {
        transactionRoot = normalizedAbsolutePath(assetsBasePath);
    }
    else if (pendingCreatedMpcResources.empty() &&
             targetsStayInside(baseDir))
    {
        transactionRoot = normalizedAbsolutePath(baseDir);
    }
    if (transactionRoot.isEmpty())
    {
        QMessageBox::warning(this, tr("保存失败"),
            tr("地图、实体列表与待提交 MPC 必须位于同一个资源根内；"
               "请取消跨资源根联动保存，或先分别另存相关文件。"));
        return false;
    }
    DurableFileTransaction fileTransaction(transactionRoot);
    const auto pendingResourcesSnapshot = pendingCreatedMpcResources;
    const SharedPendingMpcRegistry sharedRegistrySnapshot =
        sharedPendingMpcRegistry();
    bool retainMpcSyncState = false;
    struct MpcSyncStateRollback
    {
        std::map<QString, QByteArray, CaseInsensitiveQStringLess>* pending = nullptr;
        const std::map<QString, QByteArray, CaseInsensitiveQStringLess>* pendingSnapshot = nullptr;
        SharedPendingMpcRegistry* shared = nullptr;
        const SharedPendingMpcRegistry* sharedSnapshot = nullptr;
        bool* retain = nullptr;
        ~MpcSyncStateRollback()
        {
            if (retain && !*retain)
            {
                *pending = *pendingSnapshot;
                *shared = *sharedSnapshot;
            }
        }
    } mpcSyncStateRollback{
        &pendingCreatedMpcResources, &pendingResourcesSnapshot,
        &sharedPendingMpcRegistry(), &sharedRegistrySnapshot,
        &retainMpcSyncState};

    if (!syncPendingMpcResourcesWithTable(&fileTransaction))
        return false;

    // getMpcFilePath 在头部路径为空时会根据当前地图文件名推导目录。另存为或首次
    // 保存会改变文件名，因此在写盘前把当前实际解析目录固化到地图头，避免保存后
    // MPC 从 mpc/map/ 或旧地图目录突然切换到新的目录。
    const std::string originalMpcPath = mapEditor.getMpcPath();
    const std::string originalMapFileName = mapEditor.getMapFileName();
    bool rollbackMpcPath = false;
    if (originalMpcPath.empty())
    {
        // Include resources currently reachable only through redo. Their byte
        // payload still belongs to the directory used before first save.
        bool hasMpcResources = !pendingCreatedMpcResources.empty();
        for (int slot = 0; slot < MAP_EDITOR_MPC_COUNT; slot++)
        {
            if (!mapEditor.getMpcInfo(slot).name.empty())
            {
                hasMpcResources = true;
                break;
            }
        }

        // An empty new map can safely derive its per-map folder from the save
        // target. If resources were already added before first save, keep the
        // directory in which they were validated/committed (historically
        // mpc/map/) so saving cannot create dangling names.
        if (originalMapFileName.empty() && !hasMpcResources)
            mapEditor.setMapFileName(fileName.toUtf8().toStdString());
        const QString resolvedMpcDirectory = resolveManagedMpcDirRelative();
        mapEditor.setMapFileName(originalMapFileName);
        mapEditor.setMpcPath(resolvedMpcDirectory.toUtf8().toStdString());
        rollbackMpcPath = true;
    }
    struct MpcPathRollback
    {
        MapFileEditor* editor = nullptr;
        std::string originalPath;
        bool* active = nullptr;
        ~MpcPathRollback()
        {
            if (editor && active && *active)
                editor->setMpcPath(originalPath);
        }
    } mpcPathRollback{&mapEditor, originalMpcPath, &rollbackMpcPath};

    // Entity list serialization reads Head.Map from MapFileEditor. Use the
    // target map name during all temporary writes, then roll back on failure.
    bool rollbackMapFileName = true;
    mapEditor.setMapFileName(fileName.toUtf8().toStdString());
    struct MapFileNameRollback
    {
        MapFileEditor* editor = nullptr;
        std::string originalName;
        bool* active = nullptr;
        ~MapFileNameRollback()
        {
            if (editor && active && *active)
                editor->setMapFileName(originalName);
        }
    } mapFileNameRollback{&mapEditor, originalMapFileName, &rollbackMapFileName};

    bool allWritesSucceeded = true;
    QString writeError;

    if (!mapEditor.saveToFile(mapTempPath.toUtf8().toStdString()))
    {
        allWritesSucceeded = false;
        writeError = fileName;
    }

    if (allWritesSucceeded && shouldSaveNpc && !canvas->saveNpcList(npcTempPath.toUtf8().toStdString()))
    {
        allWritesSucceeded = false;
        writeError = npcFileName;
    }

    if (allWritesSucceeded && shouldSaveObj && !canvas->saveObjectList(objTempPath.toUtf8().toStdString()))
    {
        allWritesSucceeded = false;
        writeError = objFileName;
    }

    if (!allWritesSucceeded)
    {
        QFile::remove(mapTempPath);
        QFile::remove(npcTempPath);
        QFile::remove(objTempPath);
        QMessageBox::warning(this, tr("错误"),
            tr("无法保存文件: %1\n原文件未修改。").arg(writeError));
        return false;
    }

    QString transactionError;
    auto addPreparedWrite = [&](const QString& target, const QString& temporary)
    {
        if (fileTransaction.addPreparedWrite(target, temporary, transactionError))
            return true;
        QFile::remove(mapTempPath);
        QFile::remove(npcTempPath);
        QFile::remove(objTempPath);
        QMessageBox::warning(this, tr("保存失败"),
            tr("无法准备多文件事务：\n%1").arg(transactionError));
        return false;
    };
    if (!addPreparedWrite(fileName, mapTempPath) ||
        (shouldSaveNpc && !addPreparedWrite(npcFileName, npcTempPath)) ||
        (shouldSaveObj && !addPreparedWrite(objFileName, objTempPath)))
    {
        return false;
    }

    if (!fileTransaction.commit(transactionError))
    {
        QMessageBox::warning(this, tr("保存失败"),
            tr("地图同代事务提交失败：\n%1").arg(transactionError));
        return false;
    }
    if (!transactionError.isEmpty())
        QMessageBox::warning(this, tr("事务清理警告"), transactionError);

    currentMapFileName = fileName;
    mapEditor.setMapFileName(fileName.toUtf8().toStdString());
    rollbackMpcPath = false;
    rollbackMapFileName = false;
    retainMpcSyncState = true;
    if (shouldSaveNpc)
    {
        currentNpcFileName = npcFileName;
        npcListLoadedFromRuntimeSave = false;
        undoRedoManager.markSaved(UndoDomain::NpcList);
    }
    if (shouldSaveObj)
    {
        currentObjectFileName = objFileName;
        objectListLoadedFromRuntimeSave = false;
        undoRedoManager.markSaved(UndoDomain::ObjectList);
    }
    undoRedoManager.markSaved(UndoDomain::Map);
    syncDirtyStateFromUndoHistory();
    finalizePendingMpcResourcesAfterSave();

    return true;
}

bool MapEditorWindow::saveMapFile()
{
    if (currentMapFileName.isEmpty())
        return false;
    return saveMapFile(currentMapFileName);
}

bool MapEditorWindow::loadNpcListFromFile(const QString& fileName, bool confirmReplacement)
{
    if (fileName.isEmpty() ||
        !canAdoptDocumentPath(currentNpcFileName, fileName))
        return false;

    if (confirmReplacement && isNpcListOpen && isNpcListModified)
    {
        int result = QMessageBox::question(
            this, tr("保存NPC列表"),
            tr("当前 NPC 列表有未保存的修改，加载另一列表前是否保存？"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (result == QMessageBox::Cancel)
            return false;
        if (result == QMessageBox::Yes)
        {
            onSaveNpcList();
            if (isNpcListModified)
                return false;
        }
    }

    if (!canvas->loadNpcList(fileName.toUtf8().toStdString()))
    {
        QMessageBox::warning(
            this, tr("加载失败"),
            tr("无法读取 NPC 列表：\n%1\n\n当前列表保持不变。").arg(fileName));
        return false;
    }

    currentNpcFileName = fileName;
    npcListLoadedFromRuntimeSave =
        isRuntimeSaveListPath(assetsBasePath, fileName);
    isNpcListOpen = true;
    isNpcListModified = false;
    resetUndoDomain(UndoDomain::NpcList);
    updateNpcListWidget();
    updateEntityTabs();
    updateEntityCountStatus();
    canvas->update();
    minimapWidget->refreshMinimap();
    emit documentStatesChanged();
    return true;
}

bool MapEditorWindow::loadObjectListFromFile(const QString& fileName, bool confirmReplacement)
{
    if (fileName.isEmpty() ||
        !canAdoptDocumentPath(currentObjectFileName, fileName))
        return false;

    if (confirmReplacement && isObjectListOpen && isObjectListModified)
    {
        int result = QMessageBox::question(
            this, tr("保存物体列表"),
            tr("当前物体列表有未保存的修改，加载另一列表前是否保存？"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (result == QMessageBox::Cancel)
            return false;
        if (result == QMessageBox::Yes)
        {
            onSaveObjectList();
            if (isObjectListModified)
                return false;
        }
    }

    if (!canvas->loadObjectList(fileName.toUtf8().toStdString()))
    {
        QMessageBox::warning(
            this, tr("加载失败"),
            tr("无法读取物体列表：\n%1\n\n当前列表保持不变。").arg(fileName));
        return false;
    }

    currentObjectFileName = fileName;
    objectListLoadedFromRuntimeSave =
        isRuntimeSaveListPath(assetsBasePath, fileName);
    isObjectListOpen = true;
    isObjectListModified = false;
    resetUndoDomain(UndoDomain::ObjectList);
    updateObjectListWidget();
    updateEntityTabs();
    updateEntityCountStatus();
    canvas->update();
    minimapWidget->refreshMinimap();
    emit documentStatesChanged();
    return true;
}

bool MapEditorWindow::saveNpcListToFile(const QString& fileName) const
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(fileName);
    if (!mutationLease)
        return false;
    if (isRuntimeSaveListPath(assetsBasePath, fileName))
        return false;
    return canvas->saveNpcList(fileName.toUtf8().toStdString());
}

bool MapEditorWindow::saveObjectListToFile(const QString& fileName) const
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(fileName);
    if (!mutationLease)
        return false;
    if (isRuntimeSaveListPath(assetsBasePath, fileName))
        return false;
    return canvas->saveObjectList(fileName.toUtf8().toStdString());
}

bool MapEditorWindow::saveNpcListAsFile(const QString& fileName)
{
    if (!isNpcListOpen || fileName.trimmed().isEmpty() ||
        isRuntimeSaveListPath(assetsBasePath, fileName) ||
        hasDocumentTargetConflict(ProjectDocumentType::NpcList, fileName) ||
        !canAdoptDocumentPath(currentNpcFileName, fileName) ||
        !saveNpcListToFile(fileName))
    {
        return false;
    }

    currentNpcFileName = fileName;
    npcListLoadedFromRuntimeSave = false;
    undoRedoManager.markSaved(UndoDomain::NpcList);
    syncDirtyStateFromUndoHistory();
    return true;
}

bool MapEditorWindow::saveObjectListAsFile(const QString& fileName)
{
    if (!isObjectListOpen || fileName.trimmed().isEmpty() ||
        isRuntimeSaveListPath(assetsBasePath, fileName) ||
        hasDocumentTargetConflict(ProjectDocumentType::ObjectList, fileName) ||
        !canAdoptDocumentPath(currentObjectFileName, fileName) ||
        !saveObjectListToFile(fileName))
    {
        return false;
    }

    currentObjectFileName = fileName;
    objectListLoadedFromRuntimeSave = false;
    undoRedoManager.markSaved(UndoDomain::ObjectList);
    syncDirtyStateFromUndoHistory();
    return true;
}

MapEditorWindow::ProjectListRestoreResult
MapEditorWindow::restoreProjectListDocuments(
    const QString& npcListFileName,
    const QString& objectListFileName)
{
    ProjectListRestoreResult result;
    auto pathsMatch = [](const QString& left, const QString& right)
    {
        return !left.isEmpty() && !right.isEmpty() &&
            normalizedAbsolutePath(left).compare(
                normalizedAbsolutePath(right),
                managedPathCaseSensitivity()) == 0;
    };

    if (npcListFileName.isEmpty())
    {
        if (isNpcListModified)
            result.npcListRestored = false;
        else if (isNpcListOpen)
            closeNpcListDocument();
    }
    else if (!isNpcListOpen ||
             !pathsMatch(currentNpcFileName, npcListFileName))
    {
        if (isNpcListModified ||
            !loadNpcListFromFile(npcListFileName, false))
        {
            result.npcListRestored = false;
            if (!isNpcListModified && isNpcListOpen)
                closeNpcListDocument();
        }
    }

    if (objectListFileName.isEmpty())
    {
        if (isObjectListModified)
            result.objectListRestored = false;
        else if (isObjectListOpen)
            closeObjectListDocument();
    }
    else if (!isObjectListOpen ||
             !pathsMatch(currentObjectFileName, objectListFileName))
    {
        if (isObjectListModified ||
            !loadObjectListFromFile(objectListFileName, false))
        {
            result.objectListRestored = false;
            if (!isObjectListModified && isObjectListOpen)
                closeObjectListDocument();
        }
    }
    return result;
}

void MapEditorWindow::onOpenMap()
{
    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        return;

    // 默认目录优先级：assets/map → assets 根目录 → 当前地图目录 → 空路径
    QString defaultDir;
    if (!assetsBasePath.isEmpty())
    {
        QString mapPath = assetsBasePath + "/map";
        if (QDir(mapPath).exists())
            defaultDir = mapPath;
        else
            defaultDir = assetsBasePath;
    }
    if (defaultDir.isEmpty() && !currentMapFileName.isEmpty())
        defaultDir = QFileInfo(currentMapFileName).absolutePath();

    QString filter = tr("地图文件 (*.map);;所有文件 (*.*)");
    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("打开地图文件"),
        defaultDir,
        filter,
        nullptr,
        QFileDialog::DontResolveSymlinks);

    if (!fileName.isEmpty())
    {
        openMapFile(fileName);
    }
}

void MapEditorWindow::onNewMap()
{
    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("新建地图"));

    QFormLayout* formLayout = new QFormLayout(&dialog);

    QSpinBox* widthSpin = new QSpinBox(&dialog);
    widthSpin->setRange(1, 512);
    widthSpin->setValue(64);
    formLayout->addRow(tr("宽度:"), widthSpin);

    QSpinBox* heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(1, 512);
    heightSpin->setValue(64);
    formLayout->addRow(tr("高度:"), heightSpin);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    formLayout->addRow(buttonBox);

    if (dialog.exec() == QDialog::Accepted)
    {
        createNewMap(widthSpin->value(), heightSpin->value());
    }
}

void MapEditorWindow::onSaveMap()
{
    if (currentMapFileName.isEmpty())
    {
        onSaveMapAs();
        return;
    }
    saveMapFile();
}

void MapEditorWindow::onSaveMapAs()
{
    QString filter = tr("地图文件 (*.map);;所有文件 (*.*)");
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("保存地图文件"), currentMapFileName, filter);

    if (!fileName.isEmpty())
    {
        saveMapFile(fileName);
    }
}

void MapEditorWindow::onExportThumbnail()
{
    if (!mapEditor.isLoaded())
        return;

    QString filter = tr("PNG图片 (*.png);;所有文件 (*.*)");
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("导出缩略图"), QString(), filter);

    if (!fileName.isEmpty())
    {
        auto mutationLease = AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(fileName);
        if (!mutationLease)
        {
            QMessageBox::warning(
                this,
                tr("导出失败"),
                tr("目标资源包正在更新或进行其他写入。"));
            return;
        }
        QImage thumbnail = canvas->generateThumbnail();
        QSaveFile output(fileName);
        const bool saved = output.open(QIODevice::WriteOnly) &&
            thumbnail.save(&output, "PNG") &&
            output.commit();
        if (!saved)
        {
            output.cancelWriting();
            QMessageBox::warning(
                this, tr("导出失败"), tr("无法写入文件: %1").arg(fileName));
        }
    }
}

void MapEditorWindow::onExportBmp()
{
    if (!mapEditor.isLoaded())
        return;

    QString filter = tr("BMP图片 (*.bmp);;PNG图片 (*.png);;所有文件 (*.*)");
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("导出地图大图"), QString(), filter);

    if (!fileName.isEmpty())
    {
        auto mutationLease = AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(fileName);
        if (!mutationLease)
        {
            QMessageBox::warning(
                this,
                tr("导出失败"),
                tr("目标资源包正在更新或进行其他写入。"));
            return;
        }
        QImage fullImage = canvas->generateThumbnail(8192, 8192);
        QSaveFile output(fileName);
        bool saved = output.open(QIODevice::WriteOnly);
        if (fileName.endsWith(".bmp", Qt::CaseInsensitive))
            saved = saved && fullImage.save(&output, "BMP");
        else
            saved = saved && fullImage.save(&output, "PNG");
        saved = saved && output.commit();

        if (!saved)
        {
            output.cancelWriting();
            QMessageBox::warning(
                this, tr("导出失败"), tr("无法写入文件: %1").arg(fileName));
            return;
        }

        QMessageBox::information(this, tr("导出完成"),
            tr("地图已导出到: %1").arg(fileName));
    }
}

void MapEditorWindow::onToolSelect()
{
    canvas->setEditTool(MapEditTool::Select);
    if (statusToolLabel) statusToolLabel->setText(tr("选择"));
}

void MapEditorWindow::onToolTilePaint()
{
    canvas->setEditTool(MapEditTool::TilePaint);
    if (statusToolLabel) statusToolLabel->setText(tr("瓦片绘制"));
}

void MapEditorWindow::onToolObstaclePaint()
{
    canvas->setEditTool(MapEditTool::ObstaclePaint);
    if (statusToolLabel)
    {
        uint8_t obstacle = (uint8_t)obstacleComboBox->currentData().toInt();
        QString obstacleName;
        if (obstacle == 0x00) obstacleName = tr("可通过");
        else if (obstacle == 0x40) obstacleName = tr("透明");
        else if (obstacle == 0x60) obstacleName = tr("跳透");
        else if (obstacle == 0x80) obstacleName = tr("实体");
        else if (obstacle == 0xA0) obstacleName = tr("跳障");
        else obstacleName = QString("0x%1").arg(obstacle, 2, 16, QChar('0')).toUpper();
        statusToolLabel->setText(tr("障碍工具 [%1] 左键拾取/右键绘制").arg(obstacleName));
    }
}

void MapEditorWindow::onToolTrapPaint()
{
    canvas->setEditTool(MapEditTool::TrapPaint);
    if (statusToolLabel)
    {
        int trapIdx = trapIndexSpinBox->value();
        if (trapIdx == 0)
            statusToolLabel->setText(tr("陷阱工具 [无（清除）] 左键拾取/右键绘制"));
        else
            statusToolLabel->setText(tr("陷阱工具 [索引=%1] 左键拾取/右键绘制").arg(trapIdx));
    }
}

void MapEditorWindow::onToolNpcPlace()
{
    // 如果没有待放置实体，自动创建一个默认 NPC，避免点击地图无反馈
    if (!canvas->isPlacingNpc())
    {
        MapEntityData entity;
        entity.isNpc = true;
        entity.name = "NewNPC";
        entity.walkSpeed = 1;
        entity.dialogRadius = 1;
        entity.direction = 0;
        int placeX = canvas->getHoverTileX();
        int placeY = canvas->getHoverTileY();
        if (placeX < 0 || placeY < 0)
        {
            placeX = mapEditor.isLoaded() ? mapEditor.getWidth() / 2 : 0;
            placeY = mapEditor.isLoaded() ? mapEditor.getHeight() / 2 : 0;
        }
        entity.mapX = placeX;
        entity.mapY = placeY;
        canvas->setPlacingEntity(entity);
    }
    canvas->setEditTool(MapEditTool::NpcPlace);
    if (statusToolLabel) statusToolLabel->setText(tr("NPC放置 - 点击地图放置"));
}

void MapEditorWindow::onToolObjectPlace()
{
    // 如果没有待放置实体，自动创建一个默认 Object，避免点击地图无反馈
    if (!canvas->isPlacingObject())
    {
        MapEntityData entity;
        entity.isNpc = false;
        entity.name = "NewObj";
        entity.direction = 0;
        int placeX = canvas->getHoverTileX();
        int placeY = canvas->getHoverTileY();
        if (placeX < 0 || placeY < 0)
        {
            placeX = mapEditor.isLoaded() ? mapEditor.getWidth() / 2 : 0;
            placeY = mapEditor.isLoaded() ? mapEditor.getHeight() / 2 : 0;
        }
        entity.mapX = placeX;
        entity.mapY = placeY;
        canvas->setPlacingEntity(entity);
    }
    canvas->setEditTool(MapEditTool::ObjectPlace);
    if (statusToolLabel) statusToolLabel->setText(tr("物体放置 - 点击地图放置"));
}

void MapEditorWindow::onUndo()
{
    lastMpcResourceSyncSucceeded = true;
    undoRedoManager.undo();
    if (!lastMpcResourceSyncSucceeded)
    {
        // MpcInfoEditCommand has already changed the table when its refresh
        // callback detects a filesystem failure. Move the same command back
        // immediately so history, table and resource state remain coherent.
        suppressMpcResourceSync = true;
        lastMpcResourceSyncSucceeded = true;
        undoRedoManager.redo();
        suppressMpcResourceSync = false;
        if (statusToolLabel)
            statusToolLabel->setText(tr("撤销已回滚：MPC 资源同步失败"));
        if (!lastMpcResourceSyncSucceeded)
        {
            QMessageBox::critical(this, tr("MPC 历史恢复失败"),
                tr("撤销失败后无法完整恢复原 MPC 状态。请勿保存，并重新打开地图。"));
        }
    }
    canvas->clearSelection();
    canvas->clearEntityResImageCache();
    updateNpcListWidget();
    updateObjectListWidget();
    updateEntityCountStatus();
    clearRightInfoPanel();
    canvas->update();
    minimapWidget->refreshMinimap();
    syncDirtyStateFromUndoHistory();
}

void MapEditorWindow::onRedo()
{
    lastMpcResourceSyncSucceeded = true;
    undoRedoManager.redo();
    if (!lastMpcResourceSyncSucceeded)
    {
        suppressMpcResourceSync = true;
        lastMpcResourceSyncSucceeded = true;
        undoRedoManager.undo();
        suppressMpcResourceSync = false;
        if (statusToolLabel)
            statusToolLabel->setText(tr("重做已回滚：MPC 资源同步失败"));
        if (!lastMpcResourceSyncSucceeded)
        {
            QMessageBox::critical(this, tr("MPC 历史恢复失败"),
                tr("重做失败后无法完整恢复原 MPC 状态。请勿保存，并重新打开地图。"));
        }
    }
    canvas->clearSelection();
    canvas->clearEntityResImageCache();
    updateNpcListWidget();
    updateObjectListWidget();
    updateEntityCountStatus();
    clearRightInfoPanel();
    canvas->update();
    minimapWidget->refreshMinimap();
    syncDirtyStateFromUndoHistory();
}

void MapEditorWindow::onTileClicked(int tileX, int tileY, Qt::MouseButton button)
{
    Q_UNUSED(button);
    // 放置实体后 MapRenderCanvas 还会发出 tileClicked；此时保留刚选中的实体属性，
    // 不要用同坐标的瓦片信息覆盖它。
    if (canvas && canvas->getSelectedEntityIndex() >= 0)
        return;
    selectedInfoTileX = tileX;
    selectedInfoTileY = tileY;
    updateTileInfoPanel(tileX, tileY, true);
    if (canvas && canvas->getEditTool() == MapEditTool::Select &&
        canvas->getSelectedEntityIndex() < 0 && taskPanel && inspectTaskPage)
    {
        taskPanel->setCurrentWidget(inspectTaskPage);
    }
}

void MapEditorWindow::onTileHovered(int tileX, int tileY)
{
    if (tileX >= 0 && tileY >= 0)
    {
        if (statusCoordLabel)
        {
            statusCoordLabel->setText(QString("X:%1 Y:%2").arg(tileX).arg(tileY));
        }
        if (selectedInfoTileX < 0 || selectedInfoTileY < 0)
        {
            updateTileInfoPanel(tileX, tileY, false);
        }
    }
    else
    {
        // Mouse is on canvas but outside map diamond — keep last valid coordinates
        // so they don't flicker. Only clear the info panel.
        if (selectedInfoTileX < 0 || selectedInfoTileY < 0)
        {
            tileInfoTree->clear();
        }
    }
}

void MapEditorWindow::onEntitySelected(int index, bool isNpc)
{
    updateEntityInfoPanel(index, isNpc);
    if (taskPanel && entityTaskPage)
        taskPanel->setCurrentWidget(entityTaskPage);
    if (entityTabs)
        entityTabs->setCurrentIndex(isNpc ? 0 : 1);
    // Sync the NPC/Object list tab selection (with anti-loop guard)
    syncListSelectionForEntity(index, isNpc);
}

void MapEditorWindow::onEntityDoubleClicked(int index, bool isNpc)
{
    onEditEntityProperties(index, isNpc);
}

void MapEditorWindow::onEntityMoved(int index, bool isNpc, int newTileX, int newTileY)
{
    if (pendingMoveEntityIndex == index && pendingMoveEntityIsNpc == isNpc)
    {
        undoRedoManager.pushCommand(new EntityMoveCommand(
            index, isNpc, pendingMoveOldMapX, pendingMoveOldMapY,
            newTileX, newTileY,
            &canvas->getNpcListRef(), &canvas->getObjectListRef()));
        syncDirtyStateFromUndoHistory();
    }
    minimapWidget->update();
}

void MapEditorWindow::onEntityListChanged()
{
    updateNpcListWidget();
    updateObjectListWidget();
    // 重建后根据画布选中状态恢复列表选中项，避免"地图已选中，列表没选中"
    if (canvas->getSelectedEntityIndex() >= 0)
    {
        syncListSelectionForEntity(canvas->getSelectedEntityIndex(),
                                   canvas->isSelectedEntityNpc());
    }
    minimapWidget->update();
    updateEntityCountStatus();
}

void MapEditorWindow::onTileEdited(int tileX, int tileY)
{
    bool recordedCommand = false;
    if (hasPendingOldTileData && mapEditor.isLoaded())
    {
        MapTileData newTileData = mapEditor.getTile(tileX, tileY);
        undoRedoManager.pushCommand(new TileEditCommand(
            tileX, tileY, pendingOldTileData, newTileData, &mapEditor));
        hasPendingOldTileData = false;
        recordedCommand = true;
    }
    if (!recordedCommand)
        undoRedoManager.markDirty(UndoDomain::Map);
    syncDirtyStateFromUndoHistory();
    minimapWidget->refreshMinimap();
    refreshCurrentTileInfo();
}

void MapEditorWindow::onTileAboutToBeEdited(int tileX, int tileY)
{
    if (mapEditor.isLoaded())
    {
        pendingOldTileData = mapEditor.getTile(tileX, tileY);
        hasPendingOldTileData = true;
    }
}

void MapEditorWindow::onEntityPlaced(int index, bool isNpc)
{
    const std::vector<MapEntityData>& list = isNpc ?
        canvas->getNpcList() : canvas->getObjectList();

    if (index >= 0 && index < (int)list.size())
    {
        undoRedoManager.pushCommand(new EntityAddCommand(
            list[index], index, isNpc,
            &canvas->getNpcListRef(), &canvas->getObjectListRef()));
    }

    if (isNpc)
    {
        // 列表未打开时标记为"新建列表"，确保保存地图时能写出
        if (!isNpcListOpen)
        {
            isNpcListOpen = true;
            currentNpcFileName.clear();
        }
    }
    else
    {
        if (!isObjectListOpen)
        {
            isObjectListOpen = true;
            currentObjectFileName.clear();
        }
    }
    syncDirtyStateFromUndoHistory();
}

void MapEditorWindow::onEntityMoveStarted(int index, bool isNpc, int oldMapX, int oldMapY)
{
    pendingMoveEntityIndex = index;
    pendingMoveEntityIsNpc = isNpc;
    pendingMoveOldMapX = oldMapX;
    pendingMoveOldMapY = oldMapY;
}

void MapEditorWindow::onEntityDeleteRequested()
{
    onDeleteEntity();
}

void MapEditorWindow::onZoomChanged(float zoomLevel)
{
    if (statusZoomLabel)
    {
        statusZoomLabel->setText(QString("%1%").arg((int)(zoomLevel * 100)));
    }
    minimapWidget->update();
}

void MapEditorWindow::onLayerVisibilityChanged(int layer, bool visible)
{
    canvas->setLayerVisible(layer, visible);
}

void MapEditorWindow::onObstacleVisibilityChanged(bool visible)
{
    canvas->setObstacleVisible(visible);
}

void MapEditorWindow::onTrapVisibilityChanged(bool visible)
{
    canvas->setTrapVisible(visible);
}

void MapEditorWindow::onNpcVisibilityChanged(bool visible)
{
    canvas->setNpcVisible(visible);
}

void MapEditorWindow::onObjectVisibilityChanged(bool visible)
{
    canvas->setObjectVisible(visible);
}

void MapEditorWindow::onGridVisibilityChanged(bool visible)
{
    canvas->setGridVisible(visible);
}

void MapEditorWindow::onPaintLayerChanged(int index)
{
    int layer = index >= 0 ? paintLayerCombo->itemData(index).toInt() : 0;
    if (layer == -1)
    {
        canvas->setPaintAllLayers(true);
        return;
    }

    // 从“全部图层”切到单层时：若当时持有多层拾取画笔，应把所选层对应的拾取数据
    // 作为新的标量画笔，而不是丢掉拾取结果只回到旧的 mpc/frame。
    // 先在清除多层状态前读取副本（setPaintAllLayers(false)/setPaintLayer 会清除）。
    bool restoreFromMultiLayer = canvas->isPaintAllLayers() && canvas->hasMultiLayerPaintBrush();
    MapTileLayerData restoredLayer;
    if (restoreFromMultiLayer)
        restoredLayer = canvas->getMultiLayerPaintBrush().layer[layer];

    canvas->setPaintAllLayers(false);
    canvas->setPaintLayer(layer);

    if (restoreFromMultiLayer)
    {
        // 同步 canvas 标量画笔与左侧预览控件。阻塞信号避免触发
        // onMpcSelectionChanged/onFrameSelectionChanged 再次写入 canvas（它们本身
        // 也会清多层状态，此时多层状态已被清除，但显式 setter 仍需调用保证一致）。
        {
            QSignalBlocker mpcBlocker(mpcComboBox);
            for (int i = 0; i < mpcComboBox->count(); i++)
            {
                if (mpcComboBox->itemData(i).toInt() == restoredLayer.mpc)
                {
                    mpcComboBox->setCurrentIndex(i);
                    break;
                }
            }
        }
        // canvas 标量画笔先更新；updateFrameSpinBox 会按新 mpc 调整 frame range，
        // 必须在 setValue 之前调用，否则旧 mpc 的窄 range 会把目标 frame 夹到 0。
        canvas->setPaintMpcIndex(restoredLayer.mpc);
        canvas->setPaintFrameIndex(restoredLayer.frame);
        {
            QSignalBlocker frameBlocker(frameSpinBox);
            updateFrameSpinBox();  // 设置 range 到新 mpc 的 frameCount
            frameSpinBox->setValue(restoredLayer.frame);
        }
        updateFramePreviewGrid(restoredLayer.mpc);
        updateTilePreview();
    }
}

void MapEditorWindow::onFrameSelectionChanged(int value)
{
    canvas->setPaintFrameIndex(value);
    updateTilePreview();
    // Update highlight in frame preview grid
    if (framePreviewGrid)
    {
        int cols = framePreviewCurrentCols;
        for (int i = 0; i < framePreviewGrid->count(); ++i)
        {
            QLayoutItem* item = framePreviewGrid->itemAt(i);
            if (item && item->widget())
            {
                QPushButton* btn = qobject_cast<QPushButton*>(item->widget());
                if (btn)
                    btn->setChecked(false);
            }
        }
        int row = value / cols;
        int col = value % cols;
        QLayoutItem* item = framePreviewGrid->itemAtPosition(row, col);
        if (item && item->widget())
        {
            QPushButton* btn = qobject_cast<QPushButton*>(item->widget());
            if (btn)
                btn->setChecked(true);
        }
    }
}

void MapEditorWindow::onObstacleTypeChanged(int index)
{
    uint8_t obstacle = (uint8_t)obstacleComboBox->itemData(index).toInt();
    canvas->setPaintObstacle(obstacle);

    if (canvas->getEditTool() == MapEditTool::ObstaclePaint && statusToolLabel)
    {
        QString obstacleName;
        if (obstacle == 0x00) obstacleName = tr("可通过");
        else if (obstacle == 0x40) obstacleName = tr("透明");
        else if (obstacle == 0x60) obstacleName = tr("跳透");
        else if (obstacle == 0x80) obstacleName = tr("实体");
        else if (obstacle == 0xA0) obstacleName = tr("跳障");
        else obstacleName = QString("0x%1").arg(obstacle, 2, 16, QChar('0')).toUpper();
        statusToolLabel->setText(tr("障碍工具 [%1] 左键拾取/右键绘制").arg(obstacleName));
    }
}

void MapEditorWindow::onTrapIndexChanged(int value)
{
    canvas->setPaintTrapIndex((uint8_t)value);

    if (canvas->getEditTool() == MapEditTool::TrapPaint && statusToolLabel)
    {
        if (value == 0)
            statusToolLabel->setText(tr("陷阱工具 [无（清除）] 左键拾取/右键绘制"));
        else
            statusToolLabel->setText(tr("陷阱工具 [索引=%1] 左键拾取/右键绘制").arg(value));
    }
}

void MapEditorWindow::onNpcListSelectionChanged()
{
    QListWidgetItem* current = npcListWidget->currentItem();
    if (current)
    {
        int entityIndex = current->data(Qt::UserRole).toInt();
        updateEntityInfoPanel(entityIndex, true);
        // Sync canvas selection (with anti-loop guard)
        if (!syncingEntitySelection && canvas)
            canvas->selectEntity(entityIndex, true);
    }
}

void MapEditorWindow::onObjectListSelectionChanged()
{
    QListWidgetItem* current = objectListWidget->currentItem();
    if (current)
    {
        int entityIndex = current->data(Qt::UserRole).toInt();
        updateEntityInfoPanel(entityIndex, false);
        // Sync canvas selection (with anti-loop guard)
        if (!syncingEntitySelection && canvas)
            canvas->selectEntity(entityIndex, false);
    }
}

void MapEditorWindow::onAddNpc()
{
    // 创建默认 NPC 实体，资源在属性对话框中填写
    MapEntityData entity;
    entity.isNpc = true;
    entity.name = "NewNPC";
    entity.walkSpeed = 1;
    entity.dialogRadius = 1;
    entity.direction = 0;

    // 使用当前 hover 位置或地图中心作为默认坐标
    int placeX = canvas->getHoverTileX();
    int placeY = canvas->getHoverTileY();
    if (placeX < 0 || placeY < 0)
    {
        placeX = mapEditor.isLoaded() ? mapEditor.getWidth() / 2 : 0;
        placeY = mapEditor.isLoaded() ? mapEditor.getHeight() / 2 : 0;
    }
    entity.mapX = placeX;
    entity.mapY = placeY;

    canvas->setPlacingEntity(entity);
    canvas->setEditTool(MapEditTool::NpcPlace);
    if (statusToolLabel)
        statusToolLabel->setText(tr("NPC放置 - 点击地图放置，可在属性中填写资源"));
}

void MapEditorWindow::onAddObject()
{
    // 创建默认 Object 实体，资源在属性对话框中填写
    MapEntityData entity;
    entity.isNpc = false;
    entity.name = "NewObj";
    entity.direction = 0;

    int placeX = canvas->getHoverTileX();
    int placeY = canvas->getHoverTileY();
    if (placeX < 0 || placeY < 0)
    {
        placeX = mapEditor.isLoaded() ? mapEditor.getWidth() / 2 : 0;
        placeY = mapEditor.isLoaded() ? mapEditor.getHeight() / 2 : 0;
    }
    entity.mapX = placeX;
    entity.mapY = placeY;

    canvas->setPlacingEntity(entity);
    canvas->setEditTool(MapEditTool::ObjectPlace);
    if (statusToolLabel)
        statusToolLabel->setText(tr("物体放置 - 点击地图放置，可在属性中填写资源"));
}

void MapEditorWindow::onDeleteEntity()
{
    MapEntityData* entity = canvas->getSelectedEntity();
    if (!entity)
        return;

    int index = canvas->getSelectedEntityIndex();
    if (index < 0)
        return;

    QString entityType = entity->isNpc ? tr("NPC") : tr("物体");
    QString entityName = QString::fromUtf8(entity->name.c_str());
    int result = QMessageBox::question(this,
        tr("确认删除"),
        tr("确定要删除%1 \"%2\" 吗？").arg(entityType).arg(entityName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (result != QMessageBox::Yes)
        return;

    if (entity->isNpc)
    {
        MapEntityData entityCopy = *entity;
        canvas->deleteNpc(index);
        undoRedoManager.pushCommand(new EntityDeleteCommand(
            entityCopy, index, true,
            &canvas->getNpcListRef(), &canvas->getObjectListRef()));
        updateNpcListWidget();
        syncDirtyStateFromUndoHistory();
    }
    else
    {
        MapEntityData entityCopy = *entity;
        canvas->deleteObject(index);
        undoRedoManager.pushCommand(new EntityDeleteCommand(
            entityCopy, index, false,
            &canvas->getNpcListRef(), &canvas->getObjectListRef()));
        updateObjectListWidget();
        syncDirtyStateFromUndoHistory();
    }
}

void MapEditorWindow::onEditEntityProperties(int index, bool isNpc)
{
    std::vector<MapEntityData>& list = isNpc ?
        canvas->getNpcListRef() : canvas->getObjectListRef();

    if (index < 0 || index >= (int)list.size())
        return;

    MapEntityData& entity = list[index];
    MapEntityData oldEntityData = entity;

    EntityPropertyDialog dialog(this);
    dialog.setEntity(entity);
    dialog.setAssetsBasePath(assetsBasePath);

    if (dialog.exec() == QDialog::Accepted)
    {
        MapEntityData newEntityData = entity;
        if (!dialog.applyToEntity(newEntityData))
            return;
        if (newEntityData == oldEntityData)
            return;
        entity = newEntityData;
        canvas->clearEntityResImageCache();
        undoRedoManager.pushCommand(new EntityPropertyEditCommand(
            index, isNpc, oldEntityData, entity,
            &canvas->getNpcListRef(), &canvas->getObjectListRef()));
        syncDirtyStateFromUndoHistory();
        updateEntityInfoPanel(index, isNpc);
        if (isNpc) updateNpcListWidget();
        else updateObjectListWidget();
        canvas->update();
    }
}

void MapEditorWindow::onUndoStackChanged()
{
    if (actionUndo)
    {
        actionUndo->setEnabled(undoRedoManager.canUndo());
        if (undoRedoManager.canUndo())
            actionUndo->setToolTip(tr("撤销: %1 (Ctrl+Z)").arg(undoRedoManager.getUndoDescription()));
        else
            actionUndo->setToolTip(tr("撤销 (Ctrl+Z)"));
    }
    if (actionRedo)
    {
        actionRedo->setEnabled(undoRedoManager.canRedo());
        if (undoRedoManager.canRedo())
            actionRedo->setToolTip(tr("重做: %1 (Ctrl+Y)").arg(undoRedoManager.getRedoDescription()));
        else
            actionRedo->setToolTip(tr("重做 (Ctrl+Y)"));
    }
}

void MapEditorWindow::updateMpcComboBox()
{
    mpcComboBox->clear();
    mpcComboBox->addItem(tr("空图层 / 清空当前层"), 0);
    mpcComboBox->setToolTip(tr("MPC=0 时将清空目标图层"));

    if (!mapEditor.isLoaded())
        return;

    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        const MpcInfoData& info = mapEditor.getMpcInfo(i);
        if (!info.name.empty())
        {
            // 显示 0-based 资源槽位号 [i]；data 存储值 = i + 1（文件格式 stored value）
            QString label = QString("[%1] %2").arg(i).arg(QString::fromUtf8(info.name.c_str()));
            mpcComboBox->addItem(label, i + 1);
        }
    }
}

void MapEditorWindow::updateFrameSpinBox()
{
    int mpcIndex = mpcComboBox->currentData().toInt();
    if (mpcIndex <= 0)
    {
        frameSpinBox->setRange(0, 0);
        frameSpinBox->setEnabled(false);
        updateTilePreview();
        updateFramePreviewGrid(mpcIndex);
        return;
    }

    frameSpinBox->setEnabled(true);
    std::string mpcPath = mapEditor.getMpcFilePath(mpcIndex - 1);
    int frameCount = mpcCache.getFrameCount(mpcPath);
    const int maximumStoredFrame = std::min(255, std::max(0, frameCount - 1));
    frameSpinBox->setRange(0, maximumStoredFrame);
    if (frameCount > 256)
    {
        frameSpinBox->setToolTip(tr(
            "地图图层的帧索引为 8 位，只能保存 0-255；该资源其余帧不能直接写入地图。"));
    }
    else
    {
        frameSpinBox->setToolTip(QString());
    }
    updateTilePreview();
    updateFramePreviewGrid(mpcIndex);
}

void MapEditorWindow::updateTilePreview()
{
    if (!tilePreviewLabel)
        return;

    int mpcIndex = mpcComboBox->currentData().toInt();
    if (mpcIndex <= 0)
    {
        tilePreviewLabel->clearImage(tr("将清空目标图层\n(MPC=0)"));
        return;
    }

    std::string mpcPath = mapEditor.getMpcFilePath(mpcIndex - 1);
    int frameIndex = frameSpinBox->value();
    // 始终从缓存获取原始帧图像，交给 MpcPreviewLabel 按 contentsRect 适配。
    // 控件 sizeHint 不受 pixmap 影响，因此不会形成尺寸反馈循环。
    QImage frameImage = mpcCache.getFrameImage(mpcPath, frameIndex);

    if (frameImage.isNull())
    {
        tilePreviewLabel->clearImage(tr("(无法加载)"));
        return;
    }

    tilePreviewLabel->setSourceImage(frameImage);
}

void MapEditorWindow::updateTileInfoPanel(int tileX, int tileY, bool isSelected)
{
    tileInfoTree->clear();
    if (entityInfoTree)
        entityInfoTree->clear();

    // 瓦片与实体属性分属不同任务页；选择瓦片后清除旧实体预览。
    if (entityPreviewLabel)
        entityPreviewLabel->hide();

    if (!mapEditor.isLoaded() || tileX < 0 || tileY < 0)
        return;

    QString statusLabel = isSelected ? tr("[选中]") : tr("[悬停]");
    QTreeWidgetItem* statusItem = new QTreeWidgetItem(QStringList{
        tr("状态"),
        statusLabel
    });
    if (isSelected)
    {
        QFont boldFont = statusItem->font(0);
        boldFont.setBold(true);
        statusItem->setFont(1, boldFont);
    }
    tileInfoTree->addTopLevelItem(statusItem);

    QTreeWidgetItem* coordItem = new QTreeWidgetItem(QStringList{
        tr("坐标"),
        QString("(%1, %2)").arg(tileX).arg(tileY)
    });
    tileInfoTree->addTopLevelItem(coordItem);

    for (int layer = 0; layer < 3; layer++)
    {
        MapTileLayerData layerData = mapEditor.getTileLayer(tileX, tileY, layer);
        QString layerName = tr("图层 %1").arg(layer);

        QTreeWidgetItem* layerItem = new QTreeWidgetItem(QStringList{layerName, QString()});
        tileInfoTree->addTopLevelItem(layerItem);

        // Stored value
        QTreeWidgetItem* mpcItem = new QTreeWidgetItem(QStringList{
            tr("  MPC存储值"),
            QString::number(layerData.mpc)
        });
        layerItem->addChild(mpcItem);

        if (layerData.mpc == 0)
        {
            QTreeWidgetItem* emptyItem = new QTreeWidgetItem(QStringList{
                tr("  状态"),
                tr("空图层")
            });
            layerItem->addChild(emptyItem);
        }
        else
        {
            int slotIndex = layerData.mpc - 1;  // 0-based resource slot
            QTreeWidgetItem* slotItem = new QTreeWidgetItem(QStringList{
                tr("  资源槽位"),
                QString("[%1]").arg(slotIndex)
            });
            layerItem->addChild(slotItem);

            const MpcInfoData& info = mapEditor.getMpcInfo(slotIndex);
            if (!info.name.empty())
            {
                QTreeWidgetItem* nameItem = new QTreeWidgetItem(QStringList{
                    tr("  MPC名称"),
                    QString::fromUtf8(info.name.c_str())
                });
                layerItem->addChild(nameItem);
            }

            std::string mpcPath = mapEditor.getMpcFilePath(slotIndex);
            if (!mpcPath.empty())
            {
                QTreeWidgetItem* pathItem = new QTreeWidgetItem(QStringList{
                    tr("  文件"),
                    QString::fromUtf8(mpcPath.c_str())
                });
                layerItem->addChild(pathItem);
            }
        }

        QTreeWidgetItem* frameItem = new QTreeWidgetItem(QStringList{
            tr("  Frame"),
            QString::number(layerData.frame)
        });
        layerItem->addChild(frameItem);
    }

    uint8_t obstacle = mapEditor.getTileObstacle(tileX, tileY);
    QString obstacleName;
    if (obstacle == 0x00) obstacleName = tr("可通过");
    else if (obstacle == 0x40) obstacleName = tr("透明");
    else if (obstacle == 0x60) obstacleName = tr("跳透");
    else if (obstacle == 0x80) obstacleName = tr("实体");
    else if (obstacle == 0xA0) obstacleName = tr("跳障");
    else obstacleName = QString("0x%1").arg(obstacle, 2, 16, QChar('0')).toUpper();

    QTreeWidgetItem* obstacleItem = new QTreeWidgetItem(QStringList{
        tr("障碍"),
        obstacleName
    });
    tileInfoTree->addTopLevelItem(obstacleItem);

    uint8_t trap = mapEditor.getTileTrap(tileX, tileY);
    QTreeWidgetItem* trapItem = new QTreeWidgetItem(QStringList{
        tr("陷阱"),
        trap > 0 ? QString::number(trap) : tr("无")
    });
    tileInfoTree->addTopLevelItem(trapItem);

    tileInfoTree->expandAll();
    for (int i = 0; i < tileInfoTree->columnCount(); i++)
    {
        tileInfoTree->resizeColumnToContents(i);
    }
}

void MapEditorWindow::updateEntityInfoPanel(int index, bool isNpc)
{
    if (!entityInfoTree)
        return;
    entityInfoTree->clear();
    if (tileInfoTree)
        tileInfoTree->clear();

    const std::vector<MapEntityData>& list = isNpc ?
        canvas->getNpcList() : canvas->getObjectList();

    if (index < 0 || index >= (int)list.size())
    {
        if (entityPreviewLabel)
            entityPreviewLabel->hide();
        return;
    }

    // 标记为实体选中状态（使用 canvas 当前选中瓦片坐标），避免 hover 覆盖实体信息
    selectedInfoTileX = canvas->getSelectedTileX();
    selectedInfoTileY = canvas->getSelectedTileY();

    const MapEntityData& entity = list[index];

    QTreeWidgetItem* typeItem = new QTreeWidgetItem(QStringList{
        tr("类型"),
        isNpc ? tr("NPC") : tr("物体")
    });
    entityInfoTree->addTopLevelItem(typeItem);

    QTreeWidgetItem* nameItem = new QTreeWidgetItem(QStringList{
        tr("名称"),
        QString::fromUtf8(entity.name.c_str())
    });
    entityInfoTree->addTopLevelItem(nameItem);

    QTreeWidgetItem* iniItem = new QTreeWidgetItem(QStringList{
        isNpc ? tr("NPCIni") : tr("ObjFile"),
        QString::fromUtf8(entity.iniFile.c_str())
    });
    entityInfoTree->addTopLevelItem(iniItem);

    QTreeWidgetItem* posItem = new QTreeWidgetItem(QStringList{
        tr("位置"),
        QString("(%1, %2)").arg(entity.mapX).arg(entity.mapY)
    });
    entityInfoTree->addTopLevelItem(posItem);

    QTreeWidgetItem* dirItem = new QTreeWidgetItem(QStringList{
        tr("朝向"),
        QString::number(entity.direction)
    });
    entityInfoTree->addTopLevelItem(dirItem);

    QTreeWidgetItem* kindItem = new QTreeWidgetItem(QStringList{
        tr("种类"),
        QString::number(entity.kind)
    });
    entityInfoTree->addTopLevelItem(kindItem);

    if (!entity.scriptFile.empty())
    {
        QTreeWidgetItem* scriptItem = new QTreeWidgetItem(QStringList{
        tr("脚本"),
            QString::fromUtf8(entity.scriptFile.c_str())
        });
        entityInfoTree->addTopLevelItem(scriptItem);
    }

    QTreeWidgetItem* lumItem = new QTreeWidgetItem(QStringList{
        tr("光照"),
        QString::number(entity.lum)
    });
    entityInfoTree->addTopLevelItem(lumItem);

    QTreeWidgetItem* stateItem = new QTreeWidgetItem(QStringList{
        tr("状态"),
        QString::number(entity.state)
    });
    entityInfoTree->addTopLevelItem(stateItem);

    if (isNpc)
    {
        QTreeWidgetItem* actionItem = new QTreeWidgetItem(QStringList{
        tr("漫游意图"),
            QString::number(entity.action)
        });
        entityInfoTree->addTopLevelItem(actionItem);

        QTreeWidgetItem* relationItem = new QTreeWidgetItem(QStringList{
        tr("关系"),
            QString::number(entity.relation)
        });
        entityInfoTree->addTopLevelItem(relationItem);

        QTreeWidgetItem* walkSpeedItem = new QTreeWidgetItem(QStringList{
        tr("行走速度"),
            QString::number(entity.walkSpeed)
        });
        entityInfoTree->addTopLevelItem(walkSpeedItem);

        QTreeWidgetItem* pathFinderItem = new QTreeWidgetItem(QStringList{
        tr("寻路方式"),
            QString::number(entity.pathFinder)
        });
        entityInfoTree->addTopLevelItem(pathFinderItem);

        QTreeWidgetItem* dialogRadiusItem = new QTreeWidgetItem(QStringList{
        tr("对话半径"),
            QString::number(entity.dialogRadius)
        });
        entityInfoTree->addTopLevelItem(dialogRadiusItem);

        if (entity.life > 0 || entity.lifeMax > 0)
        {
            QTreeWidgetItem* lifeItem = new QTreeWidgetItem(QStringList{
        tr("生命"),
                QString("%1/%2").arg(entity.life).arg(entity.lifeMax)
            });
            entityInfoTree->addTopLevelItem(lifeItem);
        }
    }
    else
    {
        QTreeWidgetItem* offsetItem = new QTreeWidgetItem(QStringList{
        tr("偏移"),
            QString("(%1, %2)").arg(entity.offsetX).arg(entity.offsetY)
        });
        entityInfoTree->addTopLevelItem(offsetItem);

        QTreeWidgetItem* frameItem = new QTreeWidgetItem(QStringList{
        tr("帧"),
            QString::number(entity.frame)
        });
        entityInfoTree->addTopLevelItem(frameItem);

        if (!entity.wavFile.empty())
        {
            QTreeWidgetItem* wavItem = new QTreeWidgetItem(QStringList{
        tr("音效"),
                QString::fromUtf8(entity.wavFile.c_str())
            });
            entityInfoTree->addTopLevelItem(wavItem);
        }

        if (entity.damage > 0)
        {
            QTreeWidgetItem* damageItem = new QTreeWidgetItem(QStringList{
        tr("伤害"),
                QString::number(entity.damage)
            });
            entityInfoTree->addTopLevelItem(damageItem);
        }

        if (entity.actionTime > 0)
        {
            QTreeWidgetItem* actionTimeItem = new QTreeWidgetItem(QStringList{
        tr("动作时间"),
                QString::number(entity.actionTime)
            });
            entityInfoTree->addTopLevelItem(actionTimeItem);
        }
    }

    entityInfoTree->expandAll();
    for (int i = 0; i < entityInfoTree->columnCount(); i++)
    {
        entityInfoTree->resizeColumnToContents(i);
    }

    // 实体资源预览：加载 NPC/Object 对应的 MPC 站立帧第一帧
    updateEntityPreview(entity);
}

void MapEditorWindow::updateEntityPreview(const MapEntityData& entity)
{
    if (!entityPreviewLabel)
        return;

    // 无资源文件时隐藏预览
    if (entity.iniFile.empty())
    {
        entityPreviewLabel->hide();
        return;
    }

    // 从 res ini 读取 Image 字段（与 MapRenderCanvas::drawEntity 相同逻辑）
    std::string resIniFolder = entity.isNpc ? "ini\\npcres\\" : "ini\\objres\\";
    INIFileEditor resIni;
    bool loaded = false;

    if (!mpcCache.getAssetsBasePath().empty())
        loaded = resIni.loadFromFile(mpcCache.getAssetsBasePath() + resIniFolder + entity.iniFile);
    if (!loaded)
        loaded = resIni.loadFromFile(resIniFolder + entity.iniFile);

    std::string imageName;
    if (loaded)
    {
        imageName = resIni.get("stand", "Image", "");
        if (imageName.empty())
            imageName = resIni.get("common", "Image", "");
        if (imageName.empty())
            imageName = resIni.get("walk", "Image", "");
    }

    if (imageName.empty())
    {
        entityPreviewLabel->hide();
        return;
    }

    std::string imagePath;
    for (const std::string& candidate :
         buildEditorEntityImageCandidates(imageName, entity.isNpc))
    {
        if (mpcCache.getFrameCount(candidate) > 0)
        {
            imagePath = candidate;
            break;
        }
    }
    int frameCount = imagePath.empty() ? 0 : mpcCache.getFrameCount(imagePath);
    if (frameCount <= 0)
    {
        entityPreviewLabel->hide();
        return;
    }

    int dirCount = mpcCache.getDirection(imagePath);
    int framePerDir = dirCount > 0 ? frameCount / dirCount : frameCount;
    int frameIndex = std::clamp(entity.frame, 0, frameCount - 1);
    const bool staticObject = !entity.isNpc &&
        (entity.kind == 1 || entity.kind == 3 ||
         entity.kind == 4 || entity.kind == 5);
    if (!staticObject)
    {
        frameIndex = 0;
        if (dirCount > 0 && entity.direction >= 0)
            frameIndex = (entity.direction % dirCount) * framePerDir;
    }

    QImage frameImage = mpcCache.getFrameImage(imagePath, frameIndex);
    if (frameImage.isNull())
    {
        entityPreviewLabel->hide();
        return;
    }

    // 缩放以适应预览区域，保持宽高比
    QSize availSize = entityPreviewLabel->contentsRect().size();
    if (availSize.width() < 32 || availSize.height() < 32)
        availSize = QSize(200, 150);
    QPixmap pixmap = QPixmap::fromImage(frameImage)
                         .scaled(availSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    entityPreviewLabel->setPixmap(pixmap);
    entityPreviewLabel->show();
}

void MapEditorWindow::clearRightInfoPanel()
{
    selectedInfoTileX = -1;
    selectedInfoTileY = -1;
    tileInfoTree->clear();
    if (entityInfoTree)
        entityInfoTree->clear();
    if (entityPreviewLabel)
        entityPreviewLabel->hide();
}

void MapEditorWindow::updateNpcListWidget()
{
    npcListWidget->clear();
    QString filter = npcSearchEdit ? npcSearchEdit->text().toLower() : QString();
    const auto& list = canvas->getNpcList();
    for (size_t i = 0; i < list.size(); i++)
    {
        QString name = QString::fromUtf8(list[i].name.c_str());
        if (!filter.isEmpty() && !name.toLower().contains(filter))
            continue;
        QString text = QString("[%1] %2 (%3,%4)")
            .arg(i)
            .arg(name)
            .arg(list[i].mapX)
            .arg(list[i].mapY);
        QListWidgetItem* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, (int)i);
        npcListWidget->addItem(item);
    }
    updateEntityTabs();
}

void MapEditorWindow::updateObjectListWidget()
{
    objectListWidget->clear();
    QString filter = objectSearchEdit ? objectSearchEdit->text().toLower() : QString();
    const auto& list = canvas->getObjectList();
    for (size_t i = 0; i < list.size(); i++)
    {
        QString name = QString::fromUtf8(list[i].name.c_str());
        if (!filter.isEmpty() && !name.toLower().contains(filter))
            continue;
        QString text = QString("[%1] %2 (%3,%4)")
            .arg(i)
            .arg(name)
            .arg(list[i].mapX)
            .arg(list[i].mapY);
        QListWidgetItem* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, (int)i);
        objectListWidget->addItem(item);
    }
    updateEntityTabs();
}

void MapEditorWindow::updateEntityCountStatus()
{
    if (statusEntityCountLabel)
    {
        int npcCount = (int)canvas->getNpcList().size();
        int objCount = (int)canvas->getObjectList().size();
        statusEntityCountLabel->setText(
        tr("NPC:%1 物体:%2").arg(npcCount).arg(objCount));
    }
}

void MapEditorWindow::onPickTileFromMap(int tileX, int tileY)
{
    if (!mapEditor.isLoaded())
        return;

    // 该入口当前未被任何交互路径连接，仅为保持与新模式一致而保留：
    // 全部图层模式下走多层拾取（与左键拾取同一套逻辑），单层模式下回退最高非空层。
    if (canvas->isPaintAllLayers())
    {
        MapTileData sourceTile = mapEditor.getTile(tileX, tileY);
        MapTileData brushTile;
        brushTile.layer[0] = sourceTile.layer[0];
        brushTile.layer[1] = sourceTile.layer[1];
        brushTile.layer[2] = sourceTile.layer[2];
        canvas->setMultiLayerPaintBrush(brushTile, true);
        // 先切工具再同步 UI：setEditTool 会触发 syncToolActionFromCanvas 覆盖状态栏，
        // syncPaintUIFromAllLayersPick 在最后写入“已拾取全部图层”提示，保留多层信息。
        canvas->setEditTool(MapEditTool::TilePaint);
        ui->actionToolTilePaint->setChecked(true);
        syncPaintUIFromAllLayersPick(brushTile);
        return;
    }

    bool picked = false;
    for (int layer = 2; layer >= 0; layer--)
    {
        MapTileLayerData layerData = mapEditor.getTileLayer(tileX, tileY, layer);
        if (layerData.mpc != 0)
        {
            canvas->setPaintMpcIndex(layerData.mpc);
            canvas->setPaintFrameIndex(layerData.frame);
            canvas->setPaintLayer(layer);

            for (int i = 0; i < mpcComboBox->count(); i++)
            {
                if (mpcComboBox->itemData(i).toInt() == layerData.mpc)
                {
                    mpcComboBox->setCurrentIndex(i);
                    break;
                }
            }

            frameSpinBox->setValue(layerData.frame);

            canvas->setEditTool(MapEditTool::TilePaint);
            ui->actionToolTilePaint->setChecked(true);
            if (statusToolLabel)
                statusToolLabel->setText(tr("瓦片绘制"));
            picked = true;
            break;
        }
    }

    if (!picked)
    {
        if (statusToolLabel)
            statusToolLabel->setText(tr("已拾取空 tile"));
    }
}

ClosePlan MapEditorWindow::prepareCloseTransaction() const
{
    ClosePlan plan;
    auto ask = [this](const QString& title, const QString& text)
    {
        const int result = QMessageBox::question(
            const_cast<MapEditorWindow*>(this), title, text,
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (result == QMessageBox::Cancel)
            return CloseDecision::Cancelled;
        if (result == QMessageBox::Yes)
            return CloseDecision::Save;
        return CloseDecision::Discard;
    };

    plan.decisions.append(isModified
        ? ask(tr("保存更改"), tr("地图已修改，是否保存？"))
        : CloseDecision::Ready);
    if (plan.isCancelled())
        return plan;

    plan.decisions.append(isNpcListModified && isNpcListOpen
        ? ask(tr("保存NPC列表"), tr("NPC列表有未保存的修改，是否保存？"))
        : CloseDecision::Ready);
    if (plan.isCancelled())
        return plan;

    plan.decisions.append(isObjectListModified && isObjectListOpen
        ? ask(tr("保存物体列表"), tr("物体列表有未保存的修改，是否保存？"))
        : CloseDecision::Ready);
    return plan;
}

bool MapEditorWindow::resolveCloseTransaction(const ClosePlan& plan)
{
    constexpr int mapDecisionIndex = 0;
    constexpr int npcDecisionIndex = 1;
    constexpr int objectDecisionIndex = 2;
    if (plan.decisions.size() != 3 || plan.isCancelled())
        return false;

    if (plan.decisions[mapDecisionIndex] == CloseDecision::Save)
    {
        const bool originalSaveNpcWithMap = saveNpcWithMap;
        const bool originalSaveObjectWithMap = saveObjWithMap;
        const bool originalNpcCheck = saveNpcWithMapCheck->isChecked();
        const bool originalObjectCheck = saveObjWithMapCheck->isChecked();
        const QSignalBlocker npcBlocker(saveNpcWithMapCheck);
        const QSignalBlocker objectBlocker(saveObjWithMapCheck);
        const bool saveNpc =
            plan.decisions[npcDecisionIndex] == CloseDecision::Save;
        const bool saveObject =
            plan.decisions[objectDecisionIndex] == CloseDecision::Save;
        saveNpcWithMapCheck->setChecked(saveNpc);
        saveObjWithMapCheck->setChecked(saveObject);
        saveNpcWithMap = saveNpc;
        saveObjWithMap = saveObject;

        bool saved = false;
        if (currentMapFileName.isEmpty())
        {
            onSaveMapAs();
            saved = !currentMapFileName.isEmpty() && !isModified;
        }
        else
        {
            saved = saveMapFile();
        }

        saveNpcWithMapCheck->setChecked(originalNpcCheck);
        saveObjWithMapCheck->setChecked(originalObjectCheck);
        saveNpcWithMap = originalSaveNpcWithMap;
        saveObjWithMap = originalSaveObjectWithMap;
        if (!saved)
            return false;
    }

    if (plan.decisions[npcDecisionIndex] == CloseDecision::Save &&
        isNpcListModified)
    {
        onSaveNpcList();
        if (isNpcListModified)
            return false;
    }

    if (plan.decisions[objectDecisionIndex] == CloseDecision::Save &&
        isObjectListModified)
    {
        onSaveObjectList();
        if (isObjectListModified)
            return false;
    }
    return true;
}

void MapEditorWindow::commitCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() != 3 || plan.isCancelled())
        return;

    // Discarding map changes may delete staged MPC payloads. Keep all such
    // irreversible cleanup in commit, after every editor and the project have
    // accepted the close transaction. The same pass also releases redo-only
    // payloads after a successful save.
    discardPendingMpcResources();
    removeWindowFromSharedPendingMpcRegistry(this);
    allowPreparedClose();
}

MapEditorWindow::SaveConfirmResult MapEditorWindow::confirmSaveIfModified()
{
    bool discardedAnyChanges = false;
    bool discardMapResources = false;

    // Check map dirty
    if (isModified)
    {
        int result = QMessageBox::question(this,
            tr("保存更改"),
            tr("地图已修改，是否保存？"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        if (result == QMessageBox::Cancel)
            return SaveConfirmResult::Cancelled;

        if (result == QMessageBox::No)
        {
            discardedAnyChanges = true;
            discardMapResources = true;
        }
        else
        {
            if (currentMapFileName.isEmpty())
            {
                onSaveMapAs();
                if (currentMapFileName.isEmpty())
                    return SaveConfirmResult::Cancelled;
            }
            else if (!saveMapFile())
            {
                return SaveConfirmResult::Cancelled;
            }
        }
    }

    // Check NPC list dirty
    if (isNpcListModified && isNpcListOpen)
    {
        int result = QMessageBox::question(this,
            tr("保存NPC列表"),
            tr("NPC列表有未保存的修改，是否保存？"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        if (result == QMessageBox::Cancel)
            return SaveConfirmResult::Cancelled;

        if (result == QMessageBox::Yes)
        {
            onSaveNpcList();
            if (isNpcListModified)
                return SaveConfirmResult::Cancelled;
        }
        else
        {
            discardedAnyChanges = true;
        }
    }

    // Check Object list dirty
    if (isObjectListModified && isObjectListOpen)
    {
        int result = QMessageBox::question(this,
            tr("保存物体列表"),
            tr("物体列表有未保存的修改，是否保存？"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        if (result == QMessageBox::Cancel)
            return SaveConfirmResult::Cancelled;

        if (result == QMessageBox::Yes)
        {
            onSaveObjectList();
            if (isObjectListModified)
                return SaveConfirmResult::Cancelled;
        }
        else
        {
            discardedAnyChanges = true;
        }
    }

    if (discardMapResources)
        discardPendingMpcResources();
    return discardedAnyChanges ? SaveConfirmResult::Discarded
                               : SaveConfirmResult::Saved;
}

void MapEditorWindow::setModified(bool modified)
{
    isModified = modified;
    updateWindowTitle();
    emit documentStatesChanged();
}

void MapEditorWindow::syncDirtyStateFromUndoHistory()
{
    isModified = undoRedoManager.isModified(UndoDomain::Map);
    isNpcListModified = undoRedoManager.isModified(UndoDomain::NpcList);
    isObjectListModified = undoRedoManager.isModified(UndoDomain::ObjectList);
    updateEntityTabs();
    updateWindowTitle();
    emit documentStatesChanged();
}

void MapEditorWindow::resetUndoDomain(UndoDomain domain)
{
    undoRedoManager.resetDomains(domain);
    syncDirtyStateFromUndoHistory();
}

void MapEditorWindow::updateWindowTitle()
{
    QString title;
    if (currentMapFileName.isEmpty())
    {
        title = tr("地图编辑器 - 未加载");
    }
    else
    {
        QFileInfo info(currentMapFileName);
        title = tr("地图编辑器 - %1%2")
            .arg(info.fileName())
            .arg(isModified ? " *" : "");

        // Append NPC/OBJ dirty markers
        QStringList dirtyMarkers;
        if (isNpcListModified)
        dirtyMarkers << tr("NPC*");
        if (isObjectListModified)
        dirtyMarkers << tr("物体*");
        if (!dirtyMarkers.isEmpty())
            title += QString(" [%1]").arg(dirtyMarkers.join(", "));
    }
    setWindowTitle(title);
}

void MapEditorWindow::onEditTrapScripts()
{
    if (!mapEditor.isLoaded())
        return;

    QString mapName = QFileInfo(currentMapFileName).completeBaseName();
    if (mapName.isEmpty())
    {
        QString mapFileName = QString::fromUtf8(mapEditor.getMapFileName());
        mapName = QFileInfo(mapFileName).completeBaseName();
    }
    if (mapName.isEmpty())
        mapName = tr("untitled");

    TrapScriptEditorDialog dialog(mapName, assetsBasePath, &mapEditor, this);
    dialog.exec();
}

void MapEditorWindow::onCopyArea()
{
    if (!canvas)
        return;

    // 基于最终选区集合拷入剪贴板，支持 Ctrl 添加 / Alt 删除后的非连续选区。
    canvas->copySelectedAreaTiles();
}

void MapEditorWindow::onPasteArea()
{
    if (!canvas || !canvas->hasClipboardData())
        return;

    int targetX = canvas->getHoverTileX();
    int targetY = canvas->getHoverTileY();

    if (targetX < 0 || targetY < 0)
        return;

    canvas->pasteArea(targetX, targetY);
}

void MapEditorWindow::onGotoTile()
{
    if (!mapEditor.isLoaded())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("跳转到坐标"));

    QFormLayout* formLayout = new QFormLayout(&dialog);

    QSpinBox* tileXSpin = new QSpinBox(&dialog);
    tileXSpin->setRange(0, mapEditor.getWidth() - 1);
    tileXSpin->setValue(canvas->getHoverTileX() >= 0 ? canvas->getHoverTileX() : 0);
    formLayout->addRow(tr("X坐标:"), tileXSpin);

    QSpinBox* tileYSpin = new QSpinBox(&dialog);
    tileYSpin->setRange(0, mapEditor.getHeight() - 1);
    tileYSpin->setValue(canvas->getHoverTileY() >= 0 ? canvas->getHoverTileY() : 0);
    formLayout->addRow(tr("Y坐标:"), tileYSpin);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    formLayout->addRow(buttonBox);

    if (dialog.exec() == QDialog::Accepted)
    {
        canvas->centerOnTile(tileXSpin->value(), tileYSpin->value());
        canvas->update();
    }
}

void MapEditorWindow::onEditMpcInfo()
{
    if (!mapEditor.isLoaded())
        return;
    if (resolveManagedMpcDirAbsolute().isEmpty())
    {
        QMessageBox::warning(this, tr("资源目录无效"),
            tr("请先设置有效的 assets 根目录，并确保地图 MPC 路径不包含绝对路径或 ..。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("MPC信息编辑器"));
    dialog.setMinimumSize(600, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);

    QTableWidget* table = new QTableWidget(&dialog);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({
        tr("索引"),
        tr("名称"),
        tr("动态"),
        tr("障碍"),
        tr("序号")
    });
    table->setRowCount(MAP_EDITOR_MPC_COUNT);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        const MpcInfoData& info = mapEditor.getMpcInfo(i);

        QTableWidgetItem* indexItem = new QTableWidgetItem(QString::number(i + 1));
        indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(i, 0, indexItem);

        QTableWidgetItem* nameItem = new QTableWidgetItem(QString::fromUtf8(info.name.c_str()));
        table->setItem(i, 1, nameItem);

        QTableWidgetItem* dynamicItem = new QTableWidgetItem(QString::number(info.dynamic));
        table->setItem(i, 2, dynamicItem);

        QTableWidgetItem* obstacleItem = new QTableWidgetItem(QString::number(info.obstacle));
        table->setItem(i, 3, obstacleItem);

        QTableWidgetItem* indexFieldItem = new QTableWidgetItem(QString::number(info.index));
        table->setItem(i, 4, indexFieldItem);
    }

    table->resizeColumnsToContents();
    mainLayout->addWidget(table);

    QHBoxLayout* buttonLayout = new QHBoxLayout();

    QPushButton* addMpcButton = new QPushButton(tr("添加MPC"));
    QPushButton* removeMpcButton = new QPushButton(tr("清空选中"));

    // 菜单管理器添加：记录 (mpcName -> (源绝对路径, 是否复用))，
    // OK 且全部校验通过后统一原子提交；取消不会写入受管目录。
    // Keep the exact spelling selected by the user. Cross-platform duplicate
    // validation remains case-insensitive below, but the staging key must not
    // collapse Foo.mpc and foo.mpc and later write the wrong spelling on a
    // case-sensitive filesystem.
    std::map<QString, QPair<QString, bool>> stagedAdds;

    connect(addMpcButton, &QPushButton::clicked, this, [this, table, &stagedAdds]() {
        // 复用共享资源准备逻辑，确认管理器前不写入受管目录。
        QPair<QString, QString> picked = pickSourceMpcFile();
        if (picked.first.isEmpty())
            return;
        QString src = picked.first;
        QString mpcName = picked.second;

        QString staged;
        bool reuse = false;
        StageResult stage = prepareMpcResource(src, mpcName, staged, reuse);
        if (stage == StageResult::Conflict)
        {
            QMessageBox::warning(this, tr("同名 MPC 冲突"),
                tr("受管目录已存在同名但内容不同的 MPC \"%1\"。\n"
                   "为避免影响其他地图，编辑器不会覆盖共享资源；请先将源文件改名。")
                    .arg(mpcName));
            return;
        }
        else if (stage != StageResult::Ok)
        {
            return;
        }

        stagedAdds[mpcName] = qMakePair(staged, reuse);

        // 把名称填入第一个空槽的表格（实际写入在 OK 时统一处理）。
        for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        {
            QTableWidgetItem* nameItem = table->item(i, 1);
            if (nameItem && nameItem->text().isEmpty())
            {
                table->item(i, 1)->setText(mpcName);
                table->setCurrentCell(i, 1);
                return;
            }
        }
            QMessageBox::warning(this, tr("无空槽"),
                tr("MPC 信息表已满（255 个槽位），无法添加更多 MPC。"));
        stagedAdds.erase(mpcName);
    });

    connect(removeMpcButton, &QPushButton::clicked, this, [table]() {
        int row = table->currentRow();
        if (row >= 0)
        {
            table->item(row, 1)->setText("");
            table->item(row, 2)->setText("0");
            table->item(row, 3)->setText("0");
            table->item(row, 4)->setText("0");
        }
    });

    buttonLayout->addWidget(addMpcButton);
    buttonLayout->addWidget(removeMpcButton);
    buttonLayout->addStretch();

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    buttonLayout->addWidget(buttonBox);

    mainLayout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted)
    {
        // 收集表中的新名称/字段，构建完整新表。
        // dynamic/obstacle/index 字段：保留未修改的原始值（含历史异常值如
        // 动态=13988396、障碍=13987372），仅当用户主动编辑时做范围校验；
        // 超出范围的编辑整体拒绝提交（不静默夹取），由下面的 rejectedSlots 统一处理。
        std::vector<QString> proposedNames(MAP_EDITOR_MPC_COUNT);
        MpcInfoData newMpc[MAP_EDITOR_MPC_COUNT];
        QStringList rejectedSlots;
        for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        {
            const MpcInfoData original = mapEditor.getMpcInfo(i);
            MpcInfoData info = original;
            QTableWidgetItem* nameItem = table->item(i, 1);
            QString proposedName = nameItem ? nameItem->text() : QString();
            proposedNames[i] = proposedName;
            if (nameItem)
                info.name = proposedName.toUtf8().constData();

            QTableWidgetItem* dynamicItem = table->item(i, 2);
            if (dynamicItem)
            {
                int resolved = original.dynamic;
                MpcFieldResult result = resolveMpcIntegerField(
                    dynamicItem->text(), original.dynamic, 0, 1, resolved);
                if (result == MpcFieldResult::Invalid)
                    rejectedSlots << tr("槽位 %1 的动态值 %2（有效范围 0-1）")
                                        .arg(i + 1).arg(dynamicItem->text());
                else
                    info.dynamic = resolved;
            }
            // 障碍字段：0=空 1=透明 2=实体。历史资源中存在异常值（如 13987372），
            // 用户未修改时原样保留；主动编辑后超出 0-2 拒绝提交。
            QTableWidgetItem* obstacleItem = table->item(i, 3);
            if (obstacleItem)
            {
                int resolved = original.obstacle;
                MpcFieldResult result = resolveMpcIntegerField(
                    obstacleItem->text(), original.obstacle, 0, 2, resolved);
                if (result == MpcFieldResult::Invalid)
                    rejectedSlots << tr("槽位 %1 的障碍值 %2（有效范围 0-2）")
                                        .arg(i + 1).arg(obstacleItem->text());
                else
                    info.obstacle = resolved;
            }
            // 序号字段：0-255（0=未使用，1-based 历史格式）。未修改保留原值，
            // 主动编辑后超出范围拒绝提交。
            QTableWidgetItem* indexFieldItem = table->item(i, 4);
            if (indexFieldItem)
            {
                int resolved = original.index;
                MpcFieldResult result = resolveMpcIntegerField(
                    indexFieldItem->text(), original.index, 0, 255, resolved);
                if (result == MpcFieldResult::Invalid)
                    rejectedSlots << tr("槽位 %1 的序号 %2（有效范围 0-255）")
                                        .arg(i + 1).arg(indexFieldItem->text());
                else
                    info.index = resolved;
            }
            newMpc[i] = info;
        }
        // 任一字段主动编辑后超出范围：整体拒绝提交，提示用户修正后重试。
        if (!rejectedSlots.isEmpty())
        {
            QMessageBox::warning(this, tr("字段值超出范围"),
                tr("以下 MPC 字段的值超出有效范围，修改未保存：\n\n%1\n\n"
                                  "若要保留原始异常值，请不要修改该单元格。\n"
                                  "此字段当前 C++ 运行时未使用，保留用于格式兼容。")
                    .arg(rejectedSlots.join("\n")));
            return;
        }

        // 校验所有非空名称（非空、不重复、UTF-8 字节长度 <= 31），任一失败都不写入。
        // 重复校验时把所有非空槽位一起作为目标集合，避免多槽位改名时漏判。
        for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        {
            const QString& name = proposedNames[i];
            if (name.trimmed().isEmpty())
                continue;
            const QString oldName = QString::fromUtf8(
                mapEditor.getMpcInfo(i).name.c_str());
            if (name != oldName &&
                QString::compare(name, oldName, Qt::CaseInsensitive) == 0)
            {
                QMessageBox::warning(this, tr("名称大小写冲突"),
                    tr("槽位 %1 只修改了 MPC 名称大小写（%2 → %3）。为保证 Windows 与大小写敏感平台一致，请保留原大小写或使用不同名称。")
                        .arg(i + 1).arg(oldName).arg(name));
                return;
            }
            QByteArray utf8 = name.toUtf8();
            if (utf8.size() > 31)
            {
                QMessageBox::warning(this, tr("名称过长"),
                    tr("槽位 %1 的 MPC 名称 UTF-8 字节长度超过 31（当前 %2）。修改未保存。").arg(i).arg(utf8.size()));
                return;
            }
        }
        std::set<QString, CaseInsensitiveQStringLess> seen;
        for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        {
            const QString& name = proposedNames[i];
            if (name.trimmed().isEmpty())
                continue;
            if (seen.count(name))
            {
                QMessageBox::warning(this, tr("名称重复"),
                    tr("MPC 名称 \"%1\" 重复出现，修改未保存。").arg(name));
                return;
            }
            seen.insert(name);
        }

        // 新增或改名后的名称必须能安全解析到当前地图 MPC 目录。文件可以稍后创建；
        // 只有通过“添加 MPC”选择的资源需要在本次事务中写入。
        QStringList missingPlaceholderNames;
        if (resolveManagedMpcDirAbsolute().isEmpty())
        {
            QMessageBox::warning(this, tr("资源目录无效"),
                tr("MPC 编辑期间资源目录发生变化，修改未保存。"));
            return;
        }
        for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        {
            const QString& name = proposedNames[i];
            if (name.trimmed().isEmpty())
                continue;
            QString oldName = QString::fromUtf8(mapEditor.getMpcInfo(i).name.c_str());
            if (name == oldName)
                continue;
            QFileInfo nameInfo(name);
            if (nameInfo.fileName() != name ||
                nameInfo.suffix().compare("mpc", Qt::CaseInsensitive) != 0)
            {
                QMessageBox::warning(this, tr("名称无效"),
                    tr("槽位 %1 的名称必须是只包含文件名的 .mpc 文件。").arg(i));
                return;
            }
            const QString managedPath = resolveManagedMpcTargetPath(name);
            if (managedPath.isEmpty())
            {
                QMessageBox::warning(this, tr("资源目录无效"),
                    tr("槽位 %1 的 MPC 目标路径不再安全，修改未保存。")
                        .arg(i + 1));
                return;
            }
            if (stagedAdds.count(name) == 0 &&
                !QFileInfo::exists(managedPath))
            {
                missingPlaceholderNames.append(name);
            }
        }

        // 问题1：统计将要被清空的槽位（原非空→新空）的 Tile 引用数量。
        int totalRefsToClear = 0;
        QStringList clearedSlotNames;
        for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        {
            const MpcInfoData& oldInfo = mapEditor.getMpcInfo(i);
            if (!oldInfo.name.empty() && newMpc[i].name.empty())
            {
                totalRefsToClear += countMpcSlotReferences(i);
                clearedSlotNames << QString::fromUtf8(oldInfo.name.c_str());
            }
        }
        // 有引用时明确确认（菜单一次清空多个槽位也要正确处理）。
        if (totalRefsToClear > 0)
        {
            int confirm = QMessageBox::warning(this,
                tr("确认清理引用"),
                tr("本次修改将清空 %1 个 MPC 槽位（%2），"
                                  "受影响的 %3 个 Tile 图层引用将被清空。\n\n确定继续？")
                    .arg(clearedSlotNames.size())
                    .arg(clearedSlotNames.join(", "))
                    .arg(totalRefsToClear),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (confirm != QMessageBox::Yes)
                return;
        }


        // 所有表格校验和引用确认完成后才写资源文件。只提交最终表仍引用的新增名称。
        std::set<QString> finalNames(
            proposedNames.begin(), proposedNames.end());
        for (const auto& entry : stagedAdds)
        {
            if (finalNames.count(entry.first) == 0)
                continue;
            const QString finalPath = resolveManagedMpcTargetPath(entry.first);
            if (finalPath.isEmpty())
            {
                syncPendingMpcResourcesWithTable();
                QMessageBox::warning(this, tr("提交失败"),
                    tr("MPC 目标目录已失效，地图数据未修改。"));
                return;
            }
            if (!commitMpcResource(entry.second.first, finalPath, entry.second.second))
            {
                syncPendingMpcResourcesWithTable();
                QMessageBox::warning(this, tr("提交失败"),
                    tr("无法提交 MPC 文件 %1，地图数据未修改。").arg(entry.first));
                return;
            }
        }

        // 复用与左侧按钮相同的提交逻辑：自动收集引用清理、构建单个 MpcInfoEditCommand、
        // 写入 redo 状态、刷新（恢复当前选择）。菜单管理器和左侧按钮共用同一套业务逻辑。
        int restoreIndex = mpcComboBox ? mpcComboBox->currentData().toInt() : -1;
        commitMpcTableChange(newMpc, restoreIndex,
            totalRefsToClear > 0
                ? tr("编辑 MPC 信息表（清理 %1 处引用）").arg(totalRefsToClear)
                : tr("编辑 MPC 信息表"));
        if (!missingPlaceholderNames.isEmpty() && statusToolLabel)
        {
            statusToolLabel->setText(
                tr("MPC 占位引用已保存，以下文件尚未创建：%1")
                    .arg(missingPlaceholderNames.join(
                        QStringLiteral(", "))));
        }
    }
}

// ---------------------------------------------------------------------------
// MPC 索引增删改共享业务逻辑。左侧"MPC索引"下拉框旁的 +/✎/− 按钮与菜单中的完整
// MPC 管理器都通过下面的辅助函数完成实际工作，避免复制两份实现。
//
// 关键约束（需求6/7）：
// - MPC 信息表槽位是 0-based（内部表 index），Tile 文件中的 mpc 值是 1-based
//   （指向 255 条 MPC 列表，0 表示空图层）。三者在 UI 和换算中不得混淆。
// - 添加：选择 .mpc 文件，使用 QFileInfo(...).fileName() 保存完整文件名（含 .mpc
//   后缀），占用第一个空槽，不移动已有槽位；资源不在当前地图 MPC 目录时复制到受管
//   目录，不能保存不可解析的本机绝对路径。
// - 编辑：保持槽位号不变，可替换资源、修改 name/dynamic/obstacle/index。
// - 删除：不压缩/不移动后续槽位；有 Tile 引用时显示引用数量并要求确认，确认后清空
//   这些 layer.mpc/frame；MPC 变更与引用清理组成单个可撤销操作。
// ---------------------------------------------------------------------------

bool MapEditorWindow::refreshMpcUi(int restoreStoredMpcIndex)
{
    lastMpcResourceSyncSucceeded = suppressMpcResourceSync ||
        syncPendingMpcResourcesWithTable();

    // 问题7：先清缓存，再重建下拉框/帧数据，最后按 restoreStoredMpcIndex 恢复选择。
    // 刷新前记录当前选择（data 值），避免 updateMpcComboBox 重建后回到"空图层"。
    int recordedIndex = restoreStoredMpcIndex;
    if (recordedIndex < 0 && mpcComboBox)
        recordedIndex = mpcComboBox->currentData().toInt();

    mpcCache.clearCache();
    // MPC 表变更（含 undo/redo 走 MpcInfoEditCommand 回调刷新 UI 时）可能替换
    // 同槽位 MPC 且已用数量不变，此时 render-range 缓存键不变但实际帧尺寸已变。
    // 必须显式失效，确保下次 paintEvent 重新扫描已用 MPC 的最大帧尺寸。
    canvas->invalidateRenderRangeCache();

    // 重建期间阻止 currentIndexChanged 触发半成品预览；恢复选择后统一刷新一次。
    if (mpcComboBox)
    {
        QSignalBlocker blocker(mpcComboBox);
        updateMpcComboBox();
        int idx = mpcComboBox->findData(recordedIndex);
        if (idx >= 0)
            mpcComboBox->setCurrentIndex(idx);
        else if (mpcComboBox->count() > 0)
            mpcComboBox->setCurrentIndex(0);
    }

    // 帧范围/主预览/帧列表只基于刷新后的选择更新一次。
    int currentStoredIndex = mpcComboBox ? mpcComboBox->currentData().toInt() : 0;
    canvas->setPaintMpcIndex(currentStoredIndex);
    updateFrameSpinBox();
    canvas->update();
    minimapWidget->refreshMinimap();
    refreshCurrentTileInfo();
    return lastMpcResourceSyncSucceeded;
}

bool MapEditorWindow::validateMpcName(const QString& name, int excludeSlot,
                                      const std::set<int>& extraExcludeSlots) const
{
    if (name.trimmed().isEmpty())
    {
        QMessageBox::warning(nullptr, tr("名称无效"),
            tr("MPC 名称不能为空。"));
        return false;
    }

    QByteArray utf8 = name.toUtf8();
    // MPC 名称字段固定 32 字节，运行时按 nameLen-1=31 字节截断。写入前校验字节长度，
    // 避免保存时静默截断多字节字符产生半截名称。
    if (utf8.size() > 31)
    {
        QMessageBox::warning(nullptr, tr("名称过长"),
            tr("MPC 名称 UTF-8 字节长度不能超过 31（当前 %1 字节）。").arg(utf8.size()));
        return false;
    }

    QFileInfo nameInfo(name);
    if (nameInfo.fileName() != name || nameInfo.suffix().compare("mpc", Qt::CaseInsensitive) != 0)
    {
        QMessageBox::warning(nullptr, tr("名称无效"),
            tr("MPC 名称必须是只包含文件名的 .mpc 文件，不能包含目录。"));
        return false;
    }

    if (excludeSlot >= 0 && excludeSlot < MAP_EDITOR_MPC_COUNT)
    {
        QString oldName = QString::fromUtf8(
            mapEditor.getMpcInfo(excludeSlot).name.c_str());
        if (name != oldName &&
            QString::compare(name, oldName, Qt::CaseInsensitive) == 0)
        {
            QMessageBox::warning(nullptr, tr("名称大小写冲突"),
                tr("不能只修改 MPC 名称的大小写（%1 → %2）。请保留原大小写或使用不同名称。")
                    .arg(oldName).arg(name));
            return false;
        }
    }

    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        if (i == excludeSlot || extraExcludeSlots.count(i))
            continue;
        const MpcInfoData& info = mapEditor.getMpcInfo(i);
        if (!info.name.empty() &&
            QString::compare(QString::fromUtf8(info.name.c_str()), name,
                             Qt::CaseInsensitive) == 0)
        {
            QMessageBox::warning(nullptr, tr("名称重复"),
                tr("MPC 名称 \"%1\" 已存在于槽位 %2，不能重复。").arg(name).arg(i));
            return false;
        }
    }
    return true;
}

// 问题2：受管目录必须与当前地图实际 MPC 解析目录一致。getMpcFilePath 使用：
//   头部 mpcPath 非空 → mpcPath；
//   否则 → mpc/map/<mapFileName-base>/。
QString MapEditorWindow::resolveManagedMpcDirRelative() const
{
    std::string mpcPath = mapEditor.getMpcPath();
    QString dir;
    if (!mpcPath.empty())
    {
        dir = QString::fromUtf8(mpcPath.c_str());
        dir = QDir::fromNativeSeparators(dir);
        while (dir.startsWith('/'))
            dir.remove(0, 1);
    }
    else
    {
        dir = QStringLiteral("mpc/map/");
        std::string folderName = mapEditor.getMapFileName();
        size_t lastSep = folderName.find_last_of("\\/");
        if (lastSep != std::string::npos)
            folderName = folderName.substr(lastSep + 1);
        size_t dotPos = folderName.find_last_of('.');
        if (dotPos != std::string::npos)
            folderName = folderName.substr(0, dotPos);
        if (!folderName.empty())
            dir += QString::fromUtf8(folderName.c_str()) + QStringLiteral("/");
    }
    dir = QDir::cleanPath(QDir::fromNativeSeparators(dir));
    if (dir.isEmpty() || QDir::isAbsolutePath(dir) || dir.contains(':') ||
        dir == QStringLiteral("..") || dir.startsWith(QStringLiteral("../")))
    {
        return QString();
    }
    if (dir == QStringLiteral("."))
        dir.clear();
    if (!dir.isEmpty() && !dir.endsWith('/'))
        dir += '/';
    return dir;
}

QString MapEditorWindow::resolveManagedMpcDirAbsolute() const
{
    QString rel = resolveManagedMpcDirRelative();
    if (assetsBasePath.isEmpty() || rel.isEmpty())
        return QString();
    QString target = QDir(assetsBasePath).absoluteFilePath(rel);
    if (!isPathInsideDirectory(assetsBasePath, target))
        return QString();
    return QDir::cleanPath(target);
}

QString MapEditorWindow::resolveManagedMpcTargetPath(const QString& mpcName) const
{
    QFileInfo nameInfo(mpcName);
    if (mpcName.isEmpty() || nameInfo.fileName() != mpcName ||
        nameInfo.suffix().compare("mpc", Qt::CaseInsensitive) != 0)
    {
        return QString();
    }

    const QString managedDirectory = resolveManagedMpcDirAbsolute();
    if (managedDirectory.isEmpty())
        return QString();

    const QString target = normalizedAbsolutePath(
        QDir(managedDirectory).absoluteFilePath(mpcName));
    if (!isPathLexicallyInsideDirectory(managedDirectory, target) ||
        !isPathInsideDirectory(assetsBasePath, target))
    {
        return QString();
    }
    return target;
}

bool MapEditorWindow::isPendingMpcPathSafe(const QString& absolutePath) const
{
    // Pending entries can belong to the previous map during a transactional
    // open/new operation, so validate against the stable assets root rather
    // than the newly loaded map's current MPC subdirectory.
    return !assetsBasePath.isEmpty() &&
        isPathInsideDirectory(assetsBasePath, absolutePath);
}

QPair<QString, QString> MapEditorWindow::pickSourceMpcFile()
{
    // 问题6：测试可注入文件选择器，绕过 QFileDialog 但仍走完整业务逻辑。
    if (mpcFileSelection.pickSourceFile)
    {
        QString source = mpcFileSelection.pickSourceFile();
        if (source.isEmpty())
            return {};
        QFileInfo picked(source);
        return qMakePair(picked.absoluteFilePath(), picked.fileName());
    }

    QString filter = tr("MPC文件 (*.mpc);;所有文件 (*.*)");
    QString dir = resolveManagedMpcDirAbsolute();

    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("选择MPC文件"),
        dir,
        filter,
        nullptr,
        QFileDialog::DontResolveSymlinks);
    if (fileName.isEmpty())
        return {};

    QFileInfo picked(fileName);
    return qMakePair(picked.absoluteFilePath(), picked.fileName());
}

namespace
{
// 读取文件全部字节。用于比较文件内容。
QByteArray readFileBytes(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}
}

// 资源准备阶段不写目标目录；所有校验通过后再通过 QSaveFile 原子提交。
// 同名已存在：内容相同直接复用；内容不同提示用户（覆盖/改名/取消）。
MapEditorWindow::StageResult MapEditorWindow::prepareMpcResource(
    const QString& sourceAbsPath, const QString& mpcName,
    QString& stagedAbsPathOut, bool& reuseExistingOut)
{
    reuseExistingOut = false;
    stagedAbsPathOut.clear();

    QFileInfo sourceInfo(sourceAbsPath);
    if (!sourceInfo.exists() || !sourceInfo.isFile() ||
        sourceInfo.suffix().compare("mpc", Qt::CaseInsensitive) != 0)
    {
        QMessageBox::warning(this, tr("文件无效"),
            tr("请选择存在的 .mpc 文件。"));
        return StageResult::Failed;
    }

    const QString finalAbsPath = resolveManagedMpcTargetPath(mpcName);
    if (finalAbsPath.isEmpty())
    {
        QMessageBox::warning(this, tr("资源目录无效"),
            tr("地图 MPC 路径不是 assets 根目录内的安全相对路径，不能写入资源。"));
        return StageResult::Failed;
    }

    QByteArray sourceBytes = readFileBytes(sourceAbsPath);
    if (sourceBytes.isEmpty())
        return StageResult::Failed;

    // An undo-only payload may currently be absent from disk but still be
    // owned by another editor window. Preserve its exact byte contract.
    if (sharedPendingBytesConflict(finalAbsPath, sourceBytes))
        return StageResult::Conflict;

    // A path already owned by this window remains tied to its original bytes.
    // Reusing the same bytes is safe (including redo after the file was
    // temporarily removed); different bytes must use a different name because
    // MPC table undo only records the name, not file-content versions.
    auto pending = pendingCreatedMpcResources.find(finalAbsPath);
    if (pending != pendingCreatedMpcResources.end())
    {
        if (pending->second != sourceBytes)
            return StageResult::Conflict;
        if (QFileInfo::exists(finalAbsPath))
        {
            stagedAbsPathOut = finalAbsPath;
            reuseExistingOut = true;
        }
        else
        {
            stagedAbsPathOut = sourceInfo.absoluteFilePath();
        }
        return StageResult::Ok;
    }

    // 若源已就在位（规范路径相同），直接复用。Pending ownership was
    // checked first so an externally modified staged file cannot silently
    // replace the byte version associated with undo/redo.
    QString sourceCanonical = QFileInfo(sourceAbsPath).canonicalFilePath();
    QString finalCanonical = QFileInfo(finalAbsPath).canonicalFilePath();
    if (!sourceCanonical.isEmpty() && sourceCanonical == finalCanonical)
    {
        stagedAbsPathOut = finalAbsPath;
        reuseExistingOut = true;
        return StageResult::Ok;
    }

    // 目标已存在：比较内容决定复用/冲突。
    if (QFileInfo::exists(finalAbsPath))
    {
        QByteArray existing = readFileBytes(finalAbsPath);
        if (existing == sourceBytes && !existing.isEmpty())
        {
            // 内容相同：直接复用，不重复写入，不留下临时文件。
            stagedAbsPathOut = finalAbsPath;
            reuseExistingOut = true;
            return StageResult::Ok;
        }
        // 内容不同：必须由用户决定。
        return StageResult::Conflict;
    }

    // 对话期间只记录源文件，不写受管目录。所有校验通过后由 commitMpcResource
    // 使用 QSaveFile 原子提交，因此取消、校验失败和槽位已满都不会留下临时文件。
    stagedAbsPathOut = sourceInfo.absoluteFilePath();
    return StageResult::Ok;
}

bool MapEditorWindow::commitMpcResource(const QString& stagedAbsPath, const QString& finalAbsPath, bool reuseExisting)
{
    if (stagedAbsPath.isEmpty())
        return false;

    // Never trust a path assembled before a modal dialog or filesystem
    // change. Re-resolve the current managed directory and require the caller's
    // target to be exactly the safe file in that directory.
    const QString targetPath = normalizedAbsolutePath(finalAbsPath);
    const QString expectedTarget = resolveManagedMpcTargetPath(
        QFileInfo(targetPath).fileName());
    if (expectedTarget.isEmpty() ||
        targetPath.compare(expectedTarget, managedPathCaseSensitivity()) != 0 ||
        !isPendingMpcPathSafe(targetPath))
    {
        return false;
    }
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(targetPath);
    if (!mutationLease ||
        !mutationLease.addResourcePath(stagedAbsPath))
        return false;

    const QByteArray sourceBytes = readFileBytes(stagedAbsPath);
    if (sourceBytes.isEmpty())
        return false;

    auto pending = pendingCreatedMpcResources.find(targetPath);
    if (pending != pendingCreatedMpcResources.end() && pending->second != sourceBytes)
        return false;
    if (sharedPendingBytesConflict(targetPath, sourceBytes))
        return false;

    if (reuseExisting)
    {
        return QFileInfo::exists(targetPath) &&
            readFileBytes(targetPath) == sourceBytes;
    }

    if (QFileInfo::exists(targetPath))
    {
        // Never overwrite either a shared resource or another byte version of
        // a pending resource. Identical bytes require no filesystem mutation.
        return readFileBytes(targetPath) == sourceBytes;
    }

    QDir targetDir = QFileInfo(targetPath).dir();
    if (!targetDir.exists() && !targetDir.mkpath("."))
        return false;

    QSaveFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly))
        return false;
    if (target.write(sourceBytes) != sourceBytes.size())
    {
        target.cancelWriting();
        return false;
    }
    if (!target.commit())
        return false;

    const bool ownedByThisWindow = registerSharedPendingMpcResource(
        targetPath, sourceBytes, this);
    if (ownedByThisWindow)
        pendingCreatedMpcResources[targetPath] = sourceBytes;
    return true;
}

bool MapEditorWindow::syncPendingMpcResourcesWithTable(
    DurableFileTransaction* transaction)
{
    const QString managedDirectory = resolveManagedMpcDirAbsolute();
    if (managedDirectory.isEmpty())
    {
        if (pendingCreatedMpcResources.empty())
        {
            updateSharedPendingMpcReferences(this, {});
            return true;
        }
        QMessageBox::warning(this, tr("资源同步失败"),
            tr("当前地图 MPC 目录已失效，未修改任何 pending 资源。"));
        return false;
    }
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(managedDirectory);
    if (!mutationLease)
        return false;

    std::set<QString, ManagedPathLess> referencedPaths;
    for (int slot = 0; slot < MAP_EDITOR_MPC_COUNT; slot++)
    {
        const MpcInfoData& info = mapEditor.getMpcInfo(slot);
        if (!info.name.empty())
        {
            const QString path = resolveManagedMpcTargetPath(
                QString::fromUtf8(info.name.c_str()));
            if (!path.isEmpty())
                referencedPaths.insert(path);
        }
    }
    QStringList failures;
    const auto pendingResourcesSnapshot = pendingCreatedMpcResources;
    const SharedPendingMpcRegistry sharedRegistrySnapshot =
        sharedPendingMpcRegistry();
    struct FileMutation
    {
        QString path;
        bool existedBefore = false;
        QByteArray bytesBefore;
        QByteArray bytesAfter;
    };
    std::vector<FileMutation> fileMutations;

    QString transactionError;
    auto writePendingBytes = [&fileMutations, transaction, &transactionError](
        const QString& path, const QByteArray& bytes)
    {
        if (transaction)
            return transaction->addBytesWrite(path, bytes, transactionError);
        const bool existedBefore = QFileInfo::exists(path);
        const QByteArray bytesBefore = existedBefore
            ? readFileBytes(path) : QByteArray();
        QSaveFile file(path);
        const bool written = file.open(QIODevice::WriteOnly) &&
            file.write(bytes) == bytes.size() && file.commit();
        if (!written)
        {
            file.cancelWriting();
            return false;
        }
        fileMutations.push_back(
            {path, existedBefore, bytesBefore, bytes});
        return true;
    };
    auto removePendingBytes = [&fileMutations, transaction, &transactionError](
        const QString& path)
    {
        if (!QFileInfo::exists(path))
            return true;
        if (transaction)
            return transaction->addRemoval(path, transactionError);
        const QByteArray bytesBefore = readFileBytes(path);
        if (!QFile::remove(path))
            return false;
        fileMutations.push_back({path, true, bytesBefore, QByteArray()});
        return true;
    };
    auto compensateFileMutations = [&fileMutations]()
    {
        bool restored = true;
        for (auto iterator = fileMutations.rbegin();
             iterator != fileMutations.rend(); ++iterator)
        {
            if (iterator->existedBefore)
            {
                QSaveFile file(iterator->path);
                const bool written = file.open(QIODevice::WriteOnly) &&
                    file.write(iterator->bytesBefore) ==
                        iterator->bytesBefore.size() && file.commit();
                if (!written)
                {
                    file.cancelWriting();
                    restored = false;
                }
            }
            else if (QFileInfo::exists(iterator->path))
            {
                // Delete only the exact bytes written by this transaction. If
                // another process changed the file in the meantime, preserve
                // it and escalate instead of deleting somebody else's data.
                if (readFileBytes(iterator->path) !=
                        iterator->bytesAfter ||
                    !QFile::remove(iterator->path))
                {
                    restored = false;
                }
            }
        }
        return restored;
    };

    updateSharedPendingMpcReferences(this, referencedPaths);
    // A second window can reference an undo-only payload without owning a
    // local pending entry. Verify (or recreate) that shared payload before the
    // adoption is allowed to become durable.
    for (const QString& path : referencedPaths)
    {
        SharedPendingMpcResource* shared = findSharedPendingMpcResource(path);
        if (!shared || shared->creator == this ||
            pendingCreatedMpcResources.count(path) != 0)
        {
            continue;
        }

        bool ready = isPendingMpcPathSafe(path);
        if (ready && QFileInfo::exists(path))
        {
            ready = readFileBytes(path) == shared->bytes;
        }
        else if (ready)
        {
            QDir parentDirectory = QFileInfo(path).dir();
            if (!parentDirectory.exists())
                ready = parentDirectory.mkpath(".");
            if (ready)
                ready = writePendingBytes(path, shared->bytes);
        }
        if (!ready)
            failures << path;
    }

    for (auto iterator = pendingCreatedMpcResources.begin();
         iterator != pendingCreatedMpcResources.end();)
    {
        const QString path = iterator->first;
        const QByteArray bytes = iterator->second;
        const bool referenced = referencedPaths.count(path) != 0;
        const bool durableOrShared =
            sharedPendingMpcResourceIsDurable(path, this);

        // Once another editor has adopted this published resource, this
        // window relinquishes deletion ownership. If it is currently needed
        // and absent, recreate the exact payload once before releasing it.
        if (durableOrShared)
        {
            bool ready = true;
            if (referenced && !QFileInfo::exists(path))
            {
                ready = isPendingMpcPathSafe(path);
                QDir parentDirectory = QFileInfo(path).dir();
                if (ready && !parentDirectory.exists())
                    ready = parentDirectory.mkpath(".");
                if (ready)
                    ready = writePendingBytes(path, bytes);
            }
            if (!ready)
            {
                failures << path;
                ++iterator;
                continue;
            }
            releaseSharedPendingMpcResource(path, this);
            iterator = pendingCreatedMpcResources.erase(iterator);
            continue;
        }

        bool synchronized = true;
        if (!isPendingMpcPathSafe(path))
        {
            synchronized = false;
        }
        else if (referenced)
        {
            if (!QFileInfo::exists(path))
            {
                QDir parentDirectory = QFileInfo(path).dir();
                if (!parentDirectory.exists() && !parentDirectory.mkpath("."))
                {
                    synchronized = false;
                }
                if (synchronized)
                    synchronized = writePendingBytes(path, bytes);
            }
            else if (readFileBytes(path) != bytes)
            {
                // An exclusive undo payload must never silently bind to an
                // externally replaced file with the same name.
                synchronized = false;
            }
        }
        else if (QFileInfo::exists(path))
        {
            // Delete only the exact bytes created by this window; never remove
            // a file another process changed after our operation.
            if (readFileBytes(path) != bytes || !removePendingBytes(path))
                synchronized = false;
        }

        if (!synchronized)
            failures << path;
        ++iterator;
    }

    if (failures.isEmpty())
    {
        for (const QString& path : referencedPaths)
        {
            SharedPendingMpcResource* shared =
                findSharedPendingMpcResource(path);
            if (shared && shared->creator != this)
                shared->durable = true;
        }
    }
    if (!failures.isEmpty())
    {
        const bool filesRestored = transaction ? true : compensateFileMutations();
        pendingCreatedMpcResources = pendingResourcesSnapshot;
        sharedPendingMpcRegistry() = sharedRegistrySnapshot;
        QMessageBox::warning(this, tr("资源回滚失败"),
            tr("以下尚未保存的 MPC 资源无法同步，请检查文件占用：\n%1%2")
                .arg(failures.join("\n"),
                     transactionError.isEmpty()
                        ? QString()
                        : tr("\n事务错误：%1").arg(transactionError)));
        if (!filesRestored)
        {
            QMessageBox::critical(this, tr("MPC 文件恢复失败"),
                tr("资源同步失败后，部分已执行的文件操作无法补偿。请勿保存，"
                   "并重新打开地图以核对 MPC 文件。"));
        }
    }
    return failures.isEmpty();
}

void MapEditorWindow::finalizePendingMpcResourcesAfterSave()
{
    const QString managedDirectory = resolveManagedMpcDirAbsolute();
    if (managedDirectory.isEmpty())
        return;

    std::set<QString, ManagedPathLess> referencedPaths;
    for (int slot = 0; slot < MAP_EDITOR_MPC_COUNT; slot++)
    {
        const MpcInfoData& info = mapEditor.getMpcInfo(slot);
        if (!info.name.empty())
        {
            const QString path = resolveManagedMpcTargetPath(
                QString::fromUtf8(info.name.c_str()));
            if (!path.isEmpty())
            {
                referencedPaths.insert(path);
                markSharedPendingMpcResourceDurable(path);
            }
        }
    }
    updateSharedPendingMpcReferences(this, referencedPaths);

    for (auto iterator = pendingCreatedMpcResources.begin();
         iterator != pendingCreatedMpcResources.end();)
    {
        if (referencedPaths.count(iterator->first) != 0 &&
            QFileInfo::exists(iterator->first) &&
            readFileBytes(iterator->first) == iterator->second)
        {
            // This resource is now part of the successfully saved map. It is
            // durable and must no longer be removed by later undo/discard.
            markSharedPendingMpcResourceDurable(iterator->first);
            releaseSharedPendingMpcResource(iterator->first, this);
            iterator = pendingCreatedMpcResources.erase(iterator);
        }
        else
        {
            // Unreferenced entries remain as redo payload even though their
            // file is currently absent.
            ++iterator;
        }
    }
}

void MapEditorWindow::discardPendingMpcResources(bool showWarnings)
{
    QStringList failures;
    for (const auto& entry : pendingCreatedMpcResources)
    {
        const QString path = entry.first;
        if (sharedPendingMpcResourceIsDurable(path, this))
        {
            // Another live/saved map adopted the published file. Relinquish
            // ownership without touching the shared resource.
            releaseSharedPendingMpcResource(path, this);
            continue;
        }

        auto mutationLease =
            AuthoringMutationGate::instance().
                acquireMutationLeaseForPath(path);
        if (!mutationLease)
        {
            failures << path;
            releaseSharedPendingMpcResource(path, this);
            continue;
        }

        bool removedOrAbsent = !QFileInfo::exists(path);
        if (!removedOrAbsent && isPendingMpcPathSafe(path) &&
            readFileBytes(path) == entry.second)
        {
            removedOrAbsent = QFile::remove(path);
        }
        if (!removedOrAbsent)
            failures << path;
        releaseSharedPendingMpcResource(path, this);
    }
    pendingCreatedMpcResources.clear();
    if (showWarnings && !failures.isEmpty())
    {
        QMessageBox::warning(this, tr("资源回滚失败"),
            tr("以下未保存 MPC 资源已被占用或外部修改，未自动删除：\n%1")
                .arg(failures.join("\n")));
    }
}

int MapEditorWindow::countMpcSlotReferences(int slotIndex) const
{
    if (!mapEditor.isLoaded() || slotIndex < 0 || slotIndex >= MAP_EDITOR_MPC_COUNT)
        return 0;

    // Tile.mpc 是 1-based（0=空），槽位是 0-based，匹配值 = slotIndex + 1。
    uint8_t matchMpc = static_cast<uint8_t>(slotIndex + 1);
    int count = 0;
    int mapWidth = mapEditor.getWidth();
    int mapHeight = mapEditor.getHeight();
    for (int y = 0; y < mapHeight; y++)
    {
        for (int x = 0; x < mapWidth; x++)
        {
            for (int layer = 0; layer < 3; layer++)
            {
                if (mapEditor.getTileLayer(x, y, layer).mpc == matchMpc)
                    count++;
            }
        }
    }
    return count;
}

bool MapEditorWindow::applyMpcEdit(int slotIndex, const MpcInfoData& newInfo,
                                   MpcInfoData (&oldMpcOut)[MAP_EDITOR_MPC_COUNT],
                                   MpcInfoData (&newMpcOut)[MAP_EDITOR_MPC_COUNT]) const
{
    if (slotIndex < 0 || slotIndex >= MAP_EDITOR_MPC_COUNT)
        return false;
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        oldMpcOut[i] = mapEditor.getMpcInfo(i);
        newMpcOut[i] = oldMpcOut[i];
    }
    newMpcOut[slotIndex] = newInfo;
    return true;
}

// 问题1：给定完整新表，找出所有"原非空→新空"的槽位，收集这些槽位的 Tile 引用清理。
// 供单槽删除（左侧）和多槽编辑（菜单）共用，保证删除任意槽位都正确处理引用。
int MapEditorWindow::collectReferenceCleanup(const MpcInfoData (&newTable)[MAP_EDITOR_MPC_COUNT],
    MpcInfoData (&oldMpcOut)[MAP_EDITOR_MPC_COUNT],
    std::map<std::pair<int,int>, MapTileData>& oldTilesOut,
    std::map<std::pair<int,int>, MapTileData>& newTilesOut) const
{
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        oldMpcOut[i] = mapEditor.getMpcInfo(i);

    int clearedCount = 0;
    if (!mapEditor.isLoaded())
        return clearedCount;

    int mapWidth = mapEditor.getWidth();
    int mapHeight = mapEditor.getHeight();
    for (int y = 0; y < mapHeight; y++)
    {
        for (int x = 0; x < mapWidth; x++)
        {
            MapTileData tile = mapEditor.getTile(x, y);
            bool touched = false;
            for (int layer = 0; layer < 3; layer++)
            {
                uint8_t mpc = tile.layer[layer].mpc;
                if (mpc == 0)
                    continue;
                int slot = mpc - 1;  // 1-based → 0-based
                // 该槽位被清空（原非空，新空）。
                if (slot >= 0 && slot < MAP_EDITOR_MPC_COUNT &&
                    !oldMpcOut[slot].name.empty() && newTable[slot].name.empty())
                {
                    tile.layer[layer].mpc = 0;
                    tile.layer[layer].frame = 0;
                    touched = true;
                    clearedCount++;
                }
            }
            if (touched)
            {
                auto key = std::make_pair(x, y);
                oldTilesOut[key] = mapEditor.getTile(x, y);
                newTilesOut[key] = tile;
            }
        }
    }
    return clearedCount;
}

// 核心提交：构建单个 MpcInfoEditCommand，写入 redo 状态，push，刷新（恢复选择）。
void MapEditorWindow::commitMpcTableChange(const MpcInfoData (&newMpc)[MAP_EDITOR_MPC_COUNT],
                                           int restoreStoredMpcIndex,
                                           const QString& description)
{
    MpcInfoData oldMpc[MAP_EDITOR_MPC_COUNT];
    std::map<std::pair<int,int>, MapTileData> oldTiles;
    std::map<std::pair<int,int>, MapTileData> newTiles;
    int cleared = collectReferenceCleanup(newMpc, oldMpc, oldTiles, newTiles);

    auto* command = new MpcInfoEditCommand(
        oldMpc, newMpc, oldTiles, newTiles, &mapEditor,
        [this, restoreStoredMpcIndex]() { refreshMpcUi(restoreStoredMpcIndex); },
        description);

    // 先写入 redo 状态（push 不自动 redo）：应用新表 + 引用清理。
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        mapEditor.setMpcInfo(i, newMpc[i]);
    for (const auto& kv : newTiles)
        mapEditor.setTile(kv.first.first, kv.first.second, kv.second);

    // MPC 表变更后失效渲染范围缓存：替换同槽位 MPC 且已用数量不变时，
    // 最大帧尺寸可能变化，必须重新扫描。
    if (canvas)
        canvas->invalidateRenderRangeCache();

    undoRedoManager.pushCommand(command);

    syncDirtyStateFromUndoHistory();
    if (!refreshMpcUi(restoreStoredMpcIndex))
    {
        suppressMpcResourceSync = true;
        undoRedoManager.undo();
        suppressMpcResourceSync = false;
        syncDirtyStateFromUndoHistory();
        if (statusToolLabel)
            statusToolLabel->setText(tr("MPC 修改已回滚：资源同步失败"));
        return;
    }
    if (statusToolLabel && cleared > 0)
        statusToolLabel->setText(tr("%1（清理 %2 处 Tile 引用）").arg(description).arg(cleared));
    else if (statusToolLabel)
        statusToolLabel->setText(description);
}

void MapEditorWindow::onAddMpcSlot()
{
    if (!mapEditor.isLoaded())
    {
        QMessageBox::warning(this, tr("未加载地图"),
            tr("请先打开或新建地图再添加 MPC。"));
        return;
    }
    if (resolveManagedMpcDirAbsolute().isEmpty())
    {
        QMessageBox::warning(this, tr("资源目录无效"),
            tr("请先设置有效的 assets 根目录，并确保地图 MPC 路径不包含绝对路径或 ..。"));
        return;
    }

    // 对话和校验阶段只记录源文件，commit 前不写入受管目录。
    QPair<QString, QString> picked = pickSourceMpcFile();
    if (picked.first.isEmpty())
        return;
    QString sourceAbs = picked.first;
    QString mpcName = picked.second;

    // 先完成名称和槽位校验，不触碰资源目录。
    if (!validateMpcName(mpcName, -1))
        return;

    int targetSlot = -1;
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        if (mapEditor.getMpcInfo(i).name.empty())
        {
            targetSlot = i;
            break;
        }
    }
    if (targetSlot < 0)
    {
        QMessageBox::warning(this, tr("无空槽"),
            tr("MPC 信息表已满（255 个槽位），无法添加更多 MPC。"));
        return;
    }

    // 名称冲突（同名已存在但内容不同）需要在准备阶段由用户决定。
    QString stagedAbs;
    bool reuseExisting = false;
    StageResult stage = prepareMpcResource(sourceAbs, mpcName, stagedAbs, reuseExisting);
    if (stage == StageResult::Conflict)
    {
        // Shared resource bytes are never overwritten from this editor. A new
        // name keeps the operation reversible and isolated to this map.
        int choice = QMessageBox::question(this,
            tr("同名 MPC 冲突"),
            tr("受管目录已存在同名但内容不同的 MPC \"%1\"。\n"
               "为避免影响其他地图，不能覆盖共享资源。是否改用新文件名？")
                .arg(mpcName),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (choice != QMessageBox::Yes)
            return;
        bool ok = false;
        QString newName = QInputDialog::getText(this,
            tr("重命名 MPC"),
            tr("输入新的 MPC 文件名（含 .mpc 后缀）："),
            QLineEdit::Normal, mpcName, &ok);
        if (!ok || newName.trimmed().isEmpty())
            return;
        mpcName = newName;
        if (!validateMpcName(mpcName, -1))
            return;
        stage = prepareMpcResource(sourceAbs, mpcName, stagedAbs, reuseExisting);
        if (stage != StageResult::Ok)
        {
            QMessageBox::warning(this, tr("资源准备失败"),
                tr("新名称仍冲突或无法使用，已取消。"));
            return;
        }
    }
    else if (stage == StageResult::Failed || stage == StageResult::Cancelled)
    {
        return;
    }

    // 所有校验和槽位确认完成后，再原子提交资源文件。
    const QString finalAbs = resolveManagedMpcTargetPath(mpcName);
    if (finalAbs.isEmpty())
    {
        QMessageBox::warning(this, tr("提交失败"),
            tr("MPC 目标目录已失效，资源未提交。"));
        return;
    }
    if (!commitMpcResource(stagedAbs, finalAbs, reuseExisting))
    {
        QMessageBox::warning(this, tr("提交失败"),
            tr("无法将 MPC 提交为 %1。").arg(finalAbs));
        return;
    }

    // 清理缓存使新资源可立即被解码（getMpcFilePath 用新名解析）。
    mpcCache.clearCache();

    MpcInfoData newInfo = mapEditor.getMpcInfo(targetSlot);
    newInfo.name = mpcName.toUtf8().constData();

    MpcInfoData newMpc[MAP_EDITOR_MPC_COUNT];
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        newMpc[i] = mapEditor.getMpcInfo(i);
    newMpc[targetSlot] = newInfo;

    commitMpcTableChange(newMpc, targetSlot + 1,
        tr("添加 MPC: %1 (槽位 %2)").arg(mpcName).arg(targetSlot));
}

void MapEditorWindow::onEditMpcSlot()
{
    if (!mapEditor.isLoaded())
        return;
    if (resolveManagedMpcDirAbsolute().isEmpty())
    {
        QMessageBox::warning(this, tr("资源目录无效"),
            tr("请先设置有效的 assets 根目录，并确保地图 MPC 路径不包含绝对路径或 ..。"));
        return;
    }

    int mpcIndex = mpcComboBox->currentData().toInt();
    if (mpcIndex <= 0)
    {
        QMessageBox::information(this, tr("未选择 MPC"),
            tr("请先在下拉框选择一个 MPC 槽位再编辑。"));
        return;
    }
    int slotIndex = mpcIndex - 1;
    MpcInfoData info = mapEditor.getMpcInfo(slotIndex);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("编辑 MPC (槽位 %1)").arg(slotIndex));
    QFormLayout* form = new QFormLayout(&dialog);

    QLineEdit* nameEdit = new QLineEdit(QString::fromUtf8(info.name.c_str()), &dialog);
    form->addRow(tr("名称:"), nameEdit);

    QPushButton* replaceButton = new QPushButton(tr("替换资源文件..."), &dialog);
    QLabel* replaceLabel = new QLabel(tr("当前: %1").arg(QString::fromUtf8(info.name.c_str())), &dialog);
    form->addRow(tr("资源:"), replaceButton);
    form->addRow(QString(), replaceLabel);

    // 数值字段使用足够宽的范围，避免 QSpinBox 在加载历史异常值（如
    // 动态=13988396、障碍=13987372）时静默夹取；提交时再按"未修改=保留原值、
    // 已修改=范围校验、超出范围=拒绝提交"的统一策略处理。
    QSpinBox* dynamicSpin = new QSpinBox(&dialog);
    dynamicSpin->setRange(INT_MIN, INT_MAX);
    dynamicSpin->setValue(info.dynamic);
    bool dynamicEdited = false;
    QObject::connect(dynamicSpin, QOverload<int>::of(&QSpinBox::valueChanged),
        [&dynamicEdited]() { dynamicEdited = true; });
    form->addRow(tr("动态:"), dynamicSpin);

    QSpinBox* obstacleSpin = new QSpinBox(&dialog);
    obstacleSpin->setRange(INT_MIN, INT_MAX);
    obstacleSpin->setValue(info.obstacle);
    bool obstacleEdited = false;
    QObject::connect(obstacleSpin, QOverload<int>::of(&QSpinBox::valueChanged),
        [&obstacleEdited]() { obstacleEdited = true; });
    // 加载时若原始值超出范围（如历史资源中的 13987372），原样显示并提示用户：
    // 不修改则保留原值，若手动改为范围外的值则无法提交。
    if (info.obstacle < 0 || info.obstacle > 2)
    {
        QMessageBox::warning(&dialog, tr("障碍值超出范围"),
            tr("槽位 %1 的原始障碍值为 %2，超出有效范围 0-2。\n"
                              "若不修改此字段，原始值将被完整保留。\n"
                              "若主动编辑为范围外的值，本次提交将被拒绝。\n"
                              "此字段当前 C++ 运行时未使用，保留用于格式兼容。")
                .arg(slotIndex + 1).arg(info.obstacle));
    }
    obstacleSpin->setToolTip(tr(
        "MPC 级障碍设置：0=空 1=透明 2=实体。\n"
        "注意：此字段与瓦片级障碍（0x40/0x60/0x80/0xA0）不同。\n"
        "当前 C++ 运行时未使用此字段，保留用于地图文件格式兼容。\n"
        "未修改时保留原始值（含历史异常值），仅在主动编辑时校验。"));
    form->addRow(tr("障碍:"), obstacleSpin);

    QSpinBox* indexSpin = new QSpinBox(&dialog);
    indexSpin->setRange(INT_MIN, INT_MAX);
    indexSpin->setValue(info.index);
    bool indexEdited = false;
    QObject::connect(indexSpin, QOverload<int>::of(&QSpinBox::valueChanged),
        [&indexEdited]() { indexEdited = true; });
    if (info.index < 0 || info.index > 255)
    {
        QMessageBox::warning(&dialog, tr("序号值超出范围"),
            tr("槽位 %1 的原始序号为 %2，超出有效范围 0-255。\n"
                              "若不修改此字段，原始值将被完整保留。\n"
                              "若主动编辑为范围外的值，本次提交将被拒绝。\n"
                              "此字段当前 C++ 运行时未使用，保留用于格式兼容。")
                .arg(slotIndex + 1).arg(info.index));
    }
    indexSpin->setToolTip(tr(
        "MPC 资源序号（历史格式为 1-based，0=未使用）。\n"
        "当前 C++ 运行时未使用此字段，保留用于地图文件格式兼容。\n"
        "未修改时保留原始值（含历史异常值），仅在主动编辑时校验。"));
    form->addRow(tr("序号:"), indexSpin);

    // 替换资源时只记录源文件；对话框确认前不写受管目录。
    QString stagedReplacementAbs;
    QString stagedReplacementName;
    bool stagedReuseExisting = false;
    connect(replaceButton, &QPushButton::clicked, this, [this, &stagedReplacementAbs,
                                                          &stagedReplacementName,
                                                          &stagedReuseExisting,
                                                          &info, replaceLabel, slotIndex]() {
        QPair<QString, QString> picked = pickSourceMpcFile();
        if (picked.first.isEmpty())
            return;
        QString src = picked.first;
        QString newName = picked.second;
        bool reuse = false;
        QString staged;
        StageResult stage = prepareMpcResource(src, newName, staged, reuse);
        if (stage == StageResult::Conflict)
        {
            int choice = QMessageBox::question(this,
                tr("同名 MPC 冲突"),
                tr("受管目录已存在同名但内容不同的 MPC \"%1\"。\n"
                   "不能覆盖共享资源，是否改用新文件名？").arg(newName),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
            if (choice != QMessageBox::Yes)
                return;
            bool accepted = false;
            QString renamed = QInputDialog::getText(this,
                tr("重命名 MPC"), tr("输入新的 MPC 文件名（含 .mpc 后缀）："),
                QLineEdit::Normal, newName, &accepted);
            if (!accepted || renamed.trimmed().isEmpty() ||
                !validateMpcName(renamed, slotIndex))
            {
                return;
            }
            newName = renamed;
            stage = prepareMpcResource(src, newName, staged, reuse);
            if (stage != StageResult::Ok)
            {
                QMessageBox::warning(this, tr("资源准备失败"),
                    tr("新名称仍冲突或无法使用。"));
                return;
            }
        }
        else if (stage != StageResult::Ok)
        {
            return;
        }
        stagedReplacementAbs = staged;
        stagedReplacementName = newName;
        stagedReuseExisting = reuse;
        replaceLabel->setText(tr("新资源: %1").arg(newName));
    });

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttonBox);

    if (dialog.exec() != QDialog::Accepted)
    {
        // 取消时尚未写入受管目录。
        return;
    }

    // 决定最终名称：若替换了资源用新文件名，否则用名称编辑框内容。
    bool replacingResource = !stagedReplacementName.isEmpty();
    QString finalName = replacingResource ? stagedReplacementName : nameEdit->text();

    if (!validateMpcName(finalName, slotIndex))
    {
        return;
    }

    const QString finalManagedPath =
        resolveManagedMpcTargetPath(finalName);
    if (finalManagedPath.isEmpty())
    {
        QMessageBox::warning(this, tr("资源目录无效"),
            tr("MPC 目标路径不安全，修改未保存。"));
        return;
    }
    const bool missingPlaceholder =
        !replacingResource &&
        !QFileInfo::exists(finalManagedPath);

    MpcInfoData newInfo = info;
    newInfo.name = finalName.toUtf8().constData();
    // 数值字段统一策略（与 MPC 管理器 onEditMpcInfo 共用同一套判定语义）：
    //   未修改 -> 保留原始值（含历史异常值如 13988396/13987372）；
    //   已修改且新值在有效范围内 -> 采用新值；
    //   已修改但超出范围 -> 拒绝本次提交（不静默夹取）。
    // 单槽编辑器用 QSpinBox + valueChanged 标志识别"主动编辑"，提交时复用
    // resolveMpcIntegerField 做最终判定，使两条写路径语义完全一致：即使把异常值
    // 改回原值，也会被判为未修改而保留，不会因为"曾触发 valueChanged"被误拒。
    newInfo.dynamic = info.dynamic;
    newInfo.obstacle = info.obstacle;
    newInfo.index = info.index;
    QStringList rejectedFields;
    if (dynamicEdited)
    {
        int resolved = info.dynamic;
        QString text = dynamicSpin->cleanText();
        MpcFieldResult result = resolveMpcIntegerField(text, info.dynamic, 0, 1, resolved);
        if (result == MpcFieldResult::Valid)
            newInfo.dynamic = resolved;
        else if (result == MpcFieldResult::Invalid)
            rejectedFields << tr("动态值 %1（有效范围 0-1）").arg(text);
        // Unmodified: 保持 newInfo.dynamic = info.dynamic（异常值原样保留）。
    }
    if (obstacleEdited)
    {
        int resolved = info.obstacle;
        QString text = obstacleSpin->cleanText();
        MpcFieldResult result = resolveMpcIntegerField(text, info.obstacle, 0, 2, resolved);
        if (result == MpcFieldResult::Valid)
            newInfo.obstacle = resolved;
        else if (result == MpcFieldResult::Invalid)
            rejectedFields << tr("障碍值 %1（有效范围 0-2）").arg(text);
    }
    if (indexEdited)
    {
        int resolved = info.index;
        QString text = indexSpin->cleanText();
        MpcFieldResult result = resolveMpcIntegerField(text, info.index, 0, 255, resolved);
        if (result == MpcFieldResult::Valid)
            newInfo.index = resolved;
        else if (result == MpcFieldResult::Invalid)
            rejectedFields << tr("序号 %1（有效范围 0-255）").arg(text);
    }
    if (!rejectedFields.isEmpty())
    {
        QMessageBox::warning(this, tr("字段值超出范围"),
            tr("以下 MPC 字段的值超出有效范围，修改未保存：\n\n%1\n\n"
                              "若要保留原始异常值，请不要修改该字段。\n"
                              "此字段当前 C++ 运行时未使用，保留用于格式兼容。")
                .arg(rejectedFields.join("\n")));
        return;
    }

    // All name and integer-field validation must finish before the first
    // filesystem mutation. Otherwise an invalid field could leave an orphaned
    // resource without a matching MPC table command.
    if (replacingResource)
    {
        const QString finalAbs = resolveManagedMpcTargetPath(finalName);
        if (finalAbs.isEmpty())
        {
            QMessageBox::warning(this, tr("提交失败"),
                tr("MPC 目标目录已失效，资源未提交。"));
            return;
        }
        if (!commitMpcResource(stagedReplacementAbs, finalAbs, stagedReuseExisting))
        {
            QMessageBox::warning(this, tr("提交失败"),
                tr("无法将 MPC 提交为 %1。").arg(finalAbs));
            return;
        }
        mpcCache.clearCache();
    }

    MpcInfoData newMpc[MAP_EDITOR_MPC_COUNT];
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        newMpc[i] = mapEditor.getMpcInfo(i);
    newMpc[slotIndex] = newInfo;

    commitMpcTableChange(newMpc, slotIndex + 1,
        tr("编辑 MPC: %1 (槽位 %2)").arg(finalName).arg(slotIndex));
    if (missingPlaceholder && statusToolLabel)
    {
        statusToolLabel->setText(
            tr("MPC 占位引用 %1 已保存；文件尚未创建。")
                .arg(finalName));
    }
}

void MapEditorWindow::onDeleteMpcSlot()
{
    if (!mapEditor.isLoaded())
        return;

    int mpcIndex = mpcComboBox->currentData().toInt();
    if (mpcIndex <= 0)
    {
        QMessageBox::information(this, tr("未选择 MPC"),
            tr("请先在下拉框选择一个 MPC 槽位再删除。"));
        return;
    }
    int slotIndex = mpcIndex - 1;
    const MpcInfoData& info = mapEditor.getMpcInfo(slotIndex);
    if (info.name.empty())
    {
        QMessageBox::information(this, tr("空槽位"),
            tr("该槽位已经是空的。"));
        return;
    }

    // 统计三个 Tile 图层中对此槽位的引用。有引用时要求明确确认，不静默删除。
    int refCount = countMpcSlotReferences(slotIndex);

    QString confirmMsg;
    if (refCount > 0)
    {
        confirmMsg = tr(
            "MPC \"%1\"（槽位 %2）当前被 %3 个 Tile 图层引用。\n"
            "删除将清空这些引用的 layer.mpc 和 frame。\n"
            "不压缩槽位，后续索引保持不变。\n\n确定删除？")
            .arg(QString::fromUtf8(info.name.c_str())).arg(slotIndex).arg(refCount);
    }
    else
    {
        confirmMsg = tr(
            "确定要删除 MPC \"%1\"（槽位 %2）吗？\n"
            "不压缩槽位，后续索引保持不变。")
            .arg(QString::fromUtf8(info.name.c_str())).arg(slotIndex);
    }

    int result = QMessageBox::warning(this,
        tr("确认删除 MPC"), confirmMsg,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result != QMessageBox::Yes)
        return;  // 用户取消后不做修改。

    // 构建新表：清空该槽位，其它保持。collectReferenceCleanup 自动收集引用清理。
    MpcInfoData newMpc[MAP_EDITOR_MPC_COUNT];
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        newMpc[i] = mapEditor.getMpcInfo(i);
    MpcInfoData cleared;
    newMpc[slotIndex] = cleared;

    // 删除后恢复选择到空项（restoreStoredMpcIndex=0 表示"空图层"项）。
    commitMpcTableChange(newMpc, 0,
        tr("删除 MPC: %1 (槽位 %2, 清理 %3 处引用)")
            .arg(QString::fromUtf8(info.name.c_str())).arg(slotIndex).arg(refCount));
}

void MapEditorWindow::onDuplicateEntity()
{
    MapEntityData* entity = canvas->getSelectedEntity();
    if (!entity || !mapEditor.isLoaded())
        return;

    int index = canvas->getSelectedEntityIndex();
    if (index < 0)
        return;

    // Copy every value needed after insertion before push_back(): the selected
    // entity lives inside the same vector and reallocation invalidates its
    // pointer.  Reading entity->isNpc afterwards was a real use-after-free in
    // Debug CRT/ASan builds.
    const bool duplicateIsNpc = entity->isNpc;
    MapEntityData newEntity = *entity;
    newEntity.mapX += 1;
    if (mapEditor.isLoaded() && newEntity.mapX >= mapEditor.getWidth())
        newEntity.mapX = entity->mapX > 0 ? entity->mapX - 1 : 0;

    if (duplicateIsNpc)
    {
        std::vector<MapEntityData>& list = canvas->getNpcListRef();
        list.push_back(newEntity);
        int newIndex = (int)list.size() - 1;
        undoRedoManager.pushCommand(new EntityAddCommand(
            newEntity, newIndex, true,
            &canvas->getNpcListRef(), &canvas->getObjectListRef()));
        canvas->selectEntity(newIndex, true);
    }
    else
    {
        std::vector<MapEntityData>& list = canvas->getObjectListRef();
        list.push_back(newEntity);
        int newIndex = (int)list.size() - 1;
        undoRedoManager.pushCommand(new EntityAddCommand(
            newEntity, newIndex, false,
            &canvas->getNpcListRef(), &canvas->getObjectListRef()));
        canvas->selectEntity(newIndex, false);
    }

    updateNpcListWidget();
    updateObjectListWidget();
    updateEntityCountStatus();
    canvas->update();
    minimapWidget->update();
    syncDirtyStateFromUndoHistory();
}

void MapEditorWindow::onSelectAll()
{
    if (!mapEditor.isLoaded())
        return;

    if (canvas->getEditTool() != MapEditTool::AreaSelect)
    {
        canvas->setEditTool(MapEditTool::AreaSelect);
        ui->actionToolAreaSelect->setChecked(true);
    }

    canvas->setAreaSelection(0, 0, mapEditor.getWidth() - 1, mapEditor.getHeight() - 1);

    if (statusToolLabel)
        statusToolLabel->setText(tr("区域选择 - 全选"));
}

void MapEditorWindow::onMapStatistics()
{
    if (!mapEditor.isLoaded())
        return;

    int mapWidth = mapEditor.getWidth();
    int mapHeight = mapEditor.getHeight();
    int totalTiles = mapWidth * mapHeight;

    int layerCount[3] = {0, 0, 0};
    int obstacleCount[6] = {0, 0, 0, 0, 0, 0};
    int trapCount = 0;
    int usedMpcCount = 0;

    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        const MpcInfoData& info = mapEditor.getMpcInfo(i);
        if (!info.name.empty())
            usedMpcCount++;
    }

    for (int y = 0; y < mapHeight; y++)
    {
        for (int x = 0; x < mapWidth; x++)
        {
            for (int layer = 0; layer < 3; layer++)
            {
                MapTileLayerData layerData = mapEditor.getTileLayer(x, y, layer);
                if (layerData.mpc != 0)
                    layerCount[layer]++;
            }

            uint8_t obstacle = mapEditor.getTileObstacle(x, y);
            if (obstacle == 0x00) obstacleCount[0]++;
            else if (obstacle == 0x40) obstacleCount[1]++;
            else if (obstacle == 0x60) obstacleCount[2]++;
            else if (obstacle == 0x80) obstacleCount[3]++;
            else if (obstacle == 0xA0) obstacleCount[4]++;
            else obstacleCount[5]++;

            uint8_t trap = mapEditor.getTileTrap(x, y);
            if (trap > 0)
                trapCount++;
        }
    }

    int npcCount = (int)canvas->getNpcList().size();
    int objCount = (int)canvas->getObjectList().size();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("地图统计信息"));
    dialog.setMinimumSize(400, 350);

    QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);

    QTreeWidget* statsTree = new QTreeWidget(&dialog);
    statsTree->setHeaderLabels({tr("属性"), tr("值")});
    statsTree->setEditTriggers(QAbstractItemView::NoEditTriggers);

    QTreeWidgetItem* dimItem = new QTreeWidgetItem(QStringList{
        tr("地图尺寸"),
        QString("%1 x %2").arg(mapWidth).arg(mapHeight)
    });
    statsTree->addTopLevelItem(dimItem);

    QTreeWidgetItem* totalItem = new QTreeWidgetItem(QStringList{
        tr("总瓦片数"),
        QString::number(totalTiles)
    });
    statsTree->addTopLevelItem(totalItem);

    QTreeWidgetItem* mpcItem = new QTreeWidgetItem(QStringList{
        tr("已使用MPC数"),
        QString("%1 / %2").arg(usedMpcCount).arg(MAP_EDITOR_MPC_COUNT)
    });
    statsTree->addTopLevelItem(mpcItem);

    QTreeWidgetItem* layerRoot = new QTreeWidgetItem(QStringList{
        tr("图层内容"),
        tr("有内容的瓦片数")
    });
    statsTree->addTopLevelItem(layerRoot);

    QString layerNames[3] = {
        tr("地面层 (Layer 0)"),
        tr("建筑层 (Layer 1)"),
        tr("空中层 (Layer 2)")
    };
    for (int i = 0; i < 3; i++)
    {
        QTreeWidgetItem* layerItem = new QTreeWidgetItem(QStringList{
            layerNames[i],
            QString("%1 (%2%)").arg(layerCount[i]).arg(totalTiles > 0 ? (100.0 * layerCount[i] / totalTiles) : 0, 0, 'f', 1)
        });
        layerRoot->addChild(layerItem);
    }

    QTreeWidgetItem* obstacleRoot = new QTreeWidgetItem(QStringList{
        tr("障碍类型"),
        tr("瓦片数")
    });
    statsTree->addTopLevelItem(obstacleRoot);

    QString obstacleNames[6] = {
        tr("可通过 (0x00)"),
        tr("透明 (0x40)"),
        tr("跳透 (0x60)"),
        tr("实体 (0x80)"),
        tr("跳障 (0xA0)"),
        tr("其他")
    };
    for (int i = 0; i < 6; i++)
    {
        QTreeWidgetItem* obsItem = new QTreeWidgetItem(QStringList{
            obstacleNames[i],
            QString("%1 (%2%)").arg(obstacleCount[i]).arg(totalTiles > 0 ? (100.0 * obstacleCount[i] / totalTiles) : 0, 0, 'f', 1)
        });
        obstacleRoot->addChild(obsItem);
    }

    QTreeWidgetItem* trapItem = new QTreeWidgetItem(QStringList{
        tr("有陷阱的瓦片"),
        QString("%1 (%2%)").arg(trapCount).arg(totalTiles > 0 ? (100.0 * trapCount / totalTiles) : 0, 0, 'f', 1)
    });
    statsTree->addTopLevelItem(trapItem);

    QTreeWidgetItem* entityRoot = new QTreeWidgetItem(QStringList{
        tr("实体"),
        tr("数量")
    });
    statsTree->addTopLevelItem(entityRoot);

    QTreeWidgetItem* npcItem = new QTreeWidgetItem(QStringList{
        tr("NPC"),
        QString::number(npcCount)
    });
    entityRoot->addChild(npcItem);

    QTreeWidgetItem* objItem = new QTreeWidgetItem(QStringList{
        tr("物体"),
        QString::number(objCount)
    });
    entityRoot->addChild(objItem);

    statsTree->expandAll();
    for (int i = 0; i < statsTree->columnCount(); i++)
        statsTree->resizeColumnToContents(i);

    mainLayout->addWidget(statsTree);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    mainLayout->addWidget(buttonBox);

    dialog.exec();
}

void MapEditorWindow::onClearAllObstacles()
{
    if (!mapEditor.isLoaded())
        return;

    int result = QMessageBox::question(this,
        tr("确认清除"),
        tr("确定要清除地图上所有障碍标记吗？此操作可撤销。"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (result != QMessageBox::Yes)
        return;

    int mapWidth = mapEditor.getWidth();
    int mapHeight = mapEditor.getHeight();

    std::map<std::pair<int,int>, MapTileData> oldTiles;
    std::map<std::pair<int,int>, MapTileData> newTiles;

    for (int y = 0; y < mapHeight; y++)
    {
        for (int x = 0; x < mapWidth; x++)
        {
            uint8_t obstacle = mapEditor.getTileObstacle(x, y);
            if (obstacle != 0)
            {
                auto key = std::make_pair(x, y);
                oldTiles[key] = mapEditor.getTile(x, y);
                mapEditor.setTileObstacle(x, y, 0);
                newTiles[key] = mapEditor.getTile(x, y);
            }
        }
    }

    if (!oldTiles.empty())
    {
        undoRedoManager.pushCommand(new TileFillCommand(oldTiles, newTiles, &mapEditor));
        syncDirtyStateFromUndoHistory();
        canvas->update();
        minimapWidget->refreshMinimap();
        if (statusToolLabel)
            statusToolLabel->setText(tr("已清除 %1 个障碍标记").arg((int)oldTiles.size()));
    }
}

void MapEditorWindow::onClearAllTraps()
{
    if (!mapEditor.isLoaded())
        return;

    int result = QMessageBox::question(this,
        tr("确认清除"),
        tr("确定要清除地图上所有陷阱标记吗？此操作可撤销。"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (result != QMessageBox::Yes)
        return;

    int mapWidth = mapEditor.getWidth();
    int mapHeight = mapEditor.getHeight();

    std::map<std::pair<int,int>, MapTileData> oldTiles;
    std::map<std::pair<int,int>, MapTileData> newTiles;

    for (int y = 0; y < mapHeight; y++)
    {
        for (int x = 0; x < mapWidth; x++)
        {
            uint8_t trap = mapEditor.getTileTrap(x, y);
            if (trap != 0)
            {
                auto key = std::make_pair(x, y);
                oldTiles[key] = mapEditor.getTile(x, y);
                mapEditor.setTileTrap(x, y, 0);
                newTiles[key] = mapEditor.getTile(x, y);
            }
        }
    }

    if (!oldTiles.empty())
    {
        undoRedoManager.pushCommand(new TileFillCommand(oldTiles, newTiles, &mapEditor));
        syncDirtyStateFromUndoHistory();
        canvas->update();
        minimapWidget->refreshMinimap();
        if (statusToolLabel)
            statusToolLabel->setText(tr("已清除 %1 个陷阱标记").arg((int)oldTiles.size()));
    }
}

void MapEditorWindow::onZoomToFit()
{
    canvas->zoomToFit();
}

void MapEditorWindow::onCoordinateVisibilityChanged(bool visible)
{
    canvas->setCoordinateVisible(visible);
}

void MapEditorWindow::refreshCurrentTileInfo()
{
    if (selectedInfoTileX >= 0 && selectedInfoTileY >= 0)
    {
        updateTileInfoPanel(selectedInfoTileX, selectedInfoTileY, true);
    }
    else
    {
        int hoverX = canvas->getHoverTileX();
        int hoverY = canvas->getHoverTileY();
        if (hoverX >= 0 && hoverY >= 0)
        {
            updateTileInfoPanel(hoverX, hoverY, false);
        }
    }
}

void MapEditorWindow::onResizeMap()
{
    if (!mapEditor.isLoaded())
        return;

    int32_t oldWidth = mapEditor.getWidth();
    int32_t oldHeight = mapEditor.getHeight();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("调整地图尺寸"));

    QFormLayout* formLayout = new QFormLayout(&dialog);

    QSpinBox* widthSpin = new QSpinBox(&dialog);
    // Real trilogy maps include dimensions below 16. Preserve the loaded
    // axis when the user changes only the other one.
    widthSpin->setRange(1, std::max(512, static_cast<int>(oldWidth)));
    widthSpin->setValue(oldWidth);
    formLayout->addRow(tr("新宽度 (列数):"), widthSpin);

    QSpinBox* heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(1, std::max(512, static_cast<int>(oldHeight)));
    heightSpin->setValue(oldHeight);
    formLayout->addRow(tr("新高度 (行数):"), heightSpin);

    QLabel* strategyLabel = new QLabel(tr(
        "策略说明：\n"
        "- 扩大：新增区域用空瓦片填充\n"
        "- 缩小：超出新边界的瓦片被裁剪\n"
        "- 越界实体将被自动移除\n"
        "- 此操作可通过撤销恢复"));
    strategyLabel->setWordWrap(true);
    formLayout->addRow(strategyLabel);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    formLayout->addRow(buttonBox);

    if (dialog.exec() != QDialog::Accepted)
        return;

    int newWidth = widthSpin->value();
    int newHeight = heightSpin->value();

    if (newWidth == oldWidth && newHeight == oldHeight)
        return;

    // 计算越界实体数量
    int outOfBoundsNpc = 0;
    int outOfBoundsObj = 0;
    if (newWidth < oldWidth || newHeight < oldHeight)
    {
        const auto& npcListConst = canvas->getNpcList();
        for (const auto& e : npcListConst)
            if (e.mapX < 0 || e.mapY < 0 || e.mapX >= newWidth || e.mapY >= newHeight)
                outOfBoundsNpc++;
        const auto& objListConst = canvas->getObjectList();
        for (const auto& e : objListConst)
            if (e.mapX < 0 || e.mapY < 0 || e.mapX >= newWidth || e.mapY >= newHeight)
                outOfBoundsObj++;
    }

    // 二次确认
    QString confirmMsg = tr("将从 %1x%2 调整为 %3x%4。")
        .arg(oldWidth).arg(oldHeight).arg(newWidth).arg(newHeight);
    if (outOfBoundsNpc > 0 || outOfBoundsObj > 0)
    {
        confirmMsg += tr("\n\n越界实体将被移除：\n- NPC: %1 个\n- 物体: %2 个")
            .arg(outOfBoundsNpc).arg(outOfBoundsObj);
    }
    confirmMsg += tr("\n\n确定继续？");

    QMessageBox::StandardButton confirm = QMessageBox::warning(this,
        tr("确认调整地图尺寸"), confirmMsg,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes)
        return;

    // Save old state before resize
    auto oldTileData = mapEditor.getTileData();
    auto oldNpcList = canvas->getNpcList();
    auto oldObjectList = canvas->getObjectList();

    MapTileData emptyFill;

    if (!mapEditor.resizeMap(newWidth, newHeight, emptyFill))
    {
        QMessageBox::warning(this, tr("错误"),
            tr("调整地图尺寸失败"));
        return;
    }

    // 裁剪越界实体
    auto& npcList = canvas->getNpcListRef();
    npcList.erase(
        std::remove_if(npcList.begin(), npcList.end(),
            [newWidth, newHeight](const MapEntityData& e) {
                return e.mapX < 0 || e.mapY < 0 ||
                       e.mapX >= newWidth || e.mapY >= newHeight;
            }),
        npcList.end());

    auto& objList = canvas->getObjectListRef();
    objList.erase(
        std::remove_if(objList.begin(), objList.end(),
            [newWidth, newHeight](const MapEntityData& e) {
                return e.mapX < 0 || e.mapY < 0 ||
                       e.mapX >= newWidth || e.mapY >= newHeight;
            }),
        objList.end());

    // Save new state after resize and entity removal
    auto newTileData = mapEditor.getTileData();
    auto newNpcList = canvas->getNpcList();
    auto newObjectList = canvas->getObjectList();

    // Create undo command with a callback to update the UI
    auto updateCallback = [this]()
    {
        int32_t w = mapEditor.getWidth();
        int32_t h = mapEditor.getHeight();
        updateNpcListWidget();
        updateObjectListWidget();
        updateEntityCountStatus();
        canvas->update();
        minimapWidget->setMapFileEditor(&mapEditor);
        minimapWidget->refreshMinimap();
        if (statusMapSizeLabel)
    statusMapSizeLabel->setText(tr("地图: %1x%2").arg(w).arg(h));
    };

    auto* command = new MapResizeCommand(
        oldWidth, oldHeight, newWidth, newHeight,
        oldTileData, newTileData,
        oldNpcList, oldObjectList, newNpcList, newObjectList,
        &mapEditor,
        &canvas->getNpcListRef(), &canvas->getObjectListRef(),
        updateCallback);

    undoRedoManager.pushCommand(command);

    syncDirtyStateFromUndoHistory();
    updateCallback();
    if (statusToolLabel)
        statusToolLabel->setText(tr("地图尺寸已调整为 %1x%2").arg(newWidth).arg(newHeight));
}

void MapEditorWindow::fillSelectedArea()
{
    if (!mapEditor.isLoaded())
        return;

    std::vector<QPoint> targetTiles = canvas->getSelectedAreaTiles();
    if (targetTiles.empty())
    {
        if (statusToolLabel)
            statusToolLabel->setText(tr("请先框选区域"));
        return;
    }

    bool allLayers = canvas->isPaintAllLayers();
    int layer = canvas->getPaintLayer();
    bool hasMultiLayer = canvas->hasMultiLayerPaintBrush();
    MapTileData multiLayerBrush = canvas->getMultiLayerPaintBrush();
    int paintMpc = canvas->getPaintMpcIndex();
    int paintFrame = canvas->getPaintFrameIndex();

    std::map<std::pair<int, int>, MapTileData> oldTiles;
    std::map<std::pair<int, int>, MapTileData> newTiles;
    for (const QPoint& tile : targetTiles)
    {
        MapTileData oldTile = mapEditor.getTile(tile.x(), tile.y());
        MapTileData newTile = oldTile;

        if (allLayers)
        {
            if (hasMultiLayer)
            {
                // 全部图层 + 多层画笔：按 layer[0..2] 分别写回。
                newTile.layer[0] = multiLayerBrush.layer[0];
                newTile.layer[1] = multiLayerBrush.layer[1];
                newTile.layer[2] = multiLayerBrush.layer[2];
            }
            else
            {
                // 全部图层 + 标量画笔：同一 mpc/frame 写入三层。
                MapTileLayerData layerData;
                layerData.frame = (uint8_t)paintFrame;
                layerData.mpc = (uint8_t)paintMpc;
                newTile.layer[0] = layerData;
                newTile.layer[1] = layerData;
                newTile.layer[2] = layerData;
            }
        }
        else
        {
            // 单图层：只写当前绘制层。
            if (layer >= 0 && layer <= 2)
            {
                MapTileLayerData layerData;
                layerData.frame = (uint8_t)paintFrame;
                layerData.mpc = (uint8_t)paintMpc;
                newTile.layer[layer] = layerData;
            }
        }

        bool changed = false;
        for (int layerIndex = 0; layerIndex < 3; layerIndex++)
        {
            changed = changed ||
                newTile.layer[layerIndex].mpc != oldTile.layer[layerIndex].mpc ||
                newTile.layer[layerIndex].frame != oldTile.layer[layerIndex].frame;
        }
        if (!changed)
            continue;

        std::pair<int, int> key = {tile.x(), tile.y()};
        oldTiles[key] = oldTile;
        newTiles[key] = newTile;
        mapEditor.setTile(tile.x(), tile.y(), newTile);
    }

    if (oldTiles.empty())
    {
        if (statusToolLabel)
            statusToolLabel->setText(tr("目标范围内没有需要填充的瓦片"));
        return;
    }

    undoRedoManager.pushCommand(new TileFillCommand(oldTiles, newTiles, &mapEditor));
    syncDirtyStateFromUndoHistory();
    canvas->update();
    minimapWidget->refreshMinimap();
    refreshCurrentTileInfo();
    if (statusToolLabel)
    {
        statusToolLabel->setText(tr("已填充选区（%1 个瓦片）").arg(oldTiles.size()));
    }
}

void MapEditorWindow::clearSelectedArea()
{
    if (!mapEditor.isLoaded())
        return;

    std::vector<QPoint> targetTiles = canvas->getSelectedAreaTiles();
    if (targetTiles.empty())
    {
        if (statusToolLabel)
            statusToolLabel->setText(tr("请先框选区域"));
        return;
    }

    bool allLayers = canvas->isPaintAllLayers();
    int layer = canvas->getPaintLayer();

    std::map<std::pair<int, int>, MapTileData> oldTiles;
    std::map<std::pair<int, int>, MapTileData> newTiles;
    for (const QPoint& tile : targetTiles)
    {
        MapTileData oldTile = mapEditor.getTile(tile.x(), tile.y());
        MapTileData newTile = oldTile;

        int firstLayer = allLayers ? 0 : layer;
        int lastLayer = allLayers ? 2 : layer;
        for (int layerIndex = firstLayer; layerIndex <= lastLayer; layerIndex++)
        {
            newTile.layer[layerIndex].mpc = 0;
            newTile.layer[layerIndex].frame = 0;
        }

        bool changed = false;
        for (int layerIndex = 0; layerIndex < 3; layerIndex++)
        {
            changed = changed ||
                newTile.layer[layerIndex].mpc != oldTile.layer[layerIndex].mpc ||
                newTile.layer[layerIndex].frame != oldTile.layer[layerIndex].frame;
        }
        if (!changed)
            continue;

        std::pair<int, int> key = {tile.x(), tile.y()};
        oldTiles[key] = oldTile;
        newTiles[key] = newTile;
        mapEditor.setTile(tile.x(), tile.y(), newTile);
    }

    if (oldTiles.empty())
    {
        if (statusToolLabel)
            statusToolLabel->setText(tr("目标范围内没有可清除的 MPC 图层"));
        return;
    }

    // 整个选区只生成一条撤销记录。完整 Tile 数据中的障碍和 trap 原样保留，
    // NPC、OBJ 等实体列表也不参与本次命令。
    undoRedoManager.pushCommand(new TileFillCommand(oldTiles, newTiles, &mapEditor));
    syncDirtyStateFromUndoHistory();
    canvas->update();
    minimapWidget->refreshMinimap();
    refreshCurrentTileInfo();
    if (statusToolLabel)
    {
        statusToolLabel->setText(tr("已清除选区的%1（%2 个瓦片）")
            .arg(allLayers ? tr("全部三层 MPC") : tr("当前 MPC 图层"))
            .arg(oldTiles.size()));
    }
}

void MapEditorWindow::onSaveNpcList()
{
    if (currentNpcFileName.isEmpty() || npcListLoadedFromRuntimeSave)
    {
        onSaveNpcListAs();
        return;
    }
    if (saveNpcListToFile(currentNpcFileName))
    {
        undoRedoManager.markSaved(UndoDomain::NpcList);
        syncDirtyStateFromUndoHistory();
        if (statusToolLabel)
            statusToolLabel->setText(tr("NPC列表已保存"));
    }
    else
    {
        QMessageBox::warning(this, tr("错误"),
            tr("无法保存NPC列表: %1").arg(currentNpcFileName));
    }
    updateEntityTabs();
}

void MapEditorWindow::onSaveNpcListAs()
{
    QString filter = tr("NPC列表文件 (*.ini *.npc);;所有文件 (*.*)");
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("保存NPC列表"), getDefaultDirectoryForList(true), filter);
    if (!fileName.isEmpty())
    {
        if (isRuntimeSaveListPath(assetsBasePath, fileName))
        {
            QMessageBox::warning(this, tr("运行时存档为只读"),
                tr("不能把 NPC 列表保存到运行时目录 save/game。\n\n"
                   "请保存到 ini/save 或其他编辑目录；运行时存档应通过专用存档编辑入口修改。"));
        }
        else if (saveNpcListAsFile(fileName))
        {
            if (statusToolLabel)
                statusToolLabel->setText(tr("NPC列表已保存"));
        }
        else
        {
            QMessageBox::warning(this, tr("错误"),
                tr("无法保存NPC列表: %1").arg(fileName));
        }
    }
    updateEntityTabs();
}

void MapEditorWindow::onCloseNpcList()
{
    if (isNpcListModified)
    {
        int result = QMessageBox::question(this,
            tr("确认关闭"),
            tr("NPC列表有未保存的修改。\n是否保存？"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (result == QMessageBox::Cancel)
            return;
        if (result == QMessageBox::Save)
        {
            onSaveNpcList();
            // If save failed or was cancelled, don't close
            if (isNpcListModified)
                return;
        }
    }

    closeNpcListDocument();
}

void MapEditorWindow::closeNpcListDocument()
{
    // Only clear canvas selection if the currently selected entity is an NPC.
    // Closing NPC list must not clear an Object selection.
    if (canvas->getSelectedEntityIndex() >= 0 && canvas->isSelectedEntityNpc())
    {
        canvas->selectEntity(-1, true);
        clearRightInfoPanel();
    }

    canvas->getNpcListRef().clear();
    currentNpcFileName.clear();
    npcListLoadedFromRuntimeSave = false;
    isNpcListOpen = false;
    isNpcListModified = false;
    resetUndoDomain(UndoDomain::NpcList);
    updateNpcListWidget();
    updateEntityTabs();
    updateEntityCountStatus();
    canvas->update();
    minimapWidget->refreshMinimap();
}

void MapEditorWindow::onSaveObjectList()
{
    if (currentObjectFileName.isEmpty() || objectListLoadedFromRuntimeSave)
    {
        onSaveObjectListAs();
        return;
    }
    if (saveObjectListToFile(currentObjectFileName))
    {
        undoRedoManager.markSaved(UndoDomain::ObjectList);
        syncDirtyStateFromUndoHistory();
        if (statusToolLabel)
            statusToolLabel->setText(tr("物体列表已保存"));
    }
    else
    {
        QMessageBox::warning(this, tr("错误"),
            tr("无法保存物体列表: %1").arg(currentObjectFileName));
    }
    updateEntityTabs();
}

void MapEditorWindow::onSaveObjectListAs()
{
    QString filter = tr("物体列表文件 (*.ini *.obj);;所有文件 (*.*)");
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("保存物体列表"), getDefaultDirectoryForList(false), filter);
    if (!fileName.isEmpty())
    {
        if (isRuntimeSaveListPath(assetsBasePath, fileName))
        {
            QMessageBox::warning(this, tr("运行时存档为只读"),
                tr("不能把物体列表保存到运行时目录 save/game。\n\n"
                   "请保存到 ini/save 或其他编辑目录；运行时存档应通过专用存档编辑入口修改。"));
        }
        else if (saveObjectListAsFile(fileName))
        {
            if (statusToolLabel)
                statusToolLabel->setText(tr("物体列表已保存"));
        }
        else
        {
            QMessageBox::warning(this, tr("错误"),
                tr("无法保存物体列表: %1").arg(fileName));
        }
    }
    updateEntityTabs();
}

void MapEditorWindow::onCloseObjectList()
{
    if (isObjectListModified)
    {
        int result = QMessageBox::question(this,
            tr("确认关闭"),
            tr("物体列表有未保存的修改。\n是否保存？"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (result == QMessageBox::Cancel)
            return;
        if (result == QMessageBox::Save)
        {
            onSaveObjectList();
            if (isObjectListModified)
                return;
        }
    }

    closeObjectListDocument();
}

void MapEditorWindow::closeObjectListDocument()
{
    // Only clear canvas selection if the currently selected entity is an Object.
    // Closing Object list must not clear an NPC selection.
    if (canvas->getSelectedEntityIndex() >= 0 && !canvas->isSelectedEntityNpc())
    {
        canvas->selectEntity(-1, false);
        clearRightInfoPanel();
    }

    canvas->getObjectListRef().clear();
    currentObjectFileName.clear();
    objectListLoadedFromRuntimeSave = false;
    isObjectListOpen = false;
    isObjectListModified = false;
    resetUndoDomain(UndoDomain::ObjectList);
    updateObjectListWidget();
    updateEntityTabs();
    updateEntityCountStatus();
    canvas->update();
    minimapWidget->refreshMinimap();
}

QString MapEditorWindow::getDefaultDirectoryForList(bool isNpc) const
{
    if (!assetsBasePath.isEmpty())
    {
        // Converted resources keep the new-game template in assets/ini/save/.
        QString iniSavePath = assetsBasePath + "/ini/save";
        if (QDir(iniSavePath).exists())
            return iniSavePath;

        // The legacy source path save/rpg0 is normalized by resource
        // conversion and is not an editable output location.
        return assetsBasePath;
    }

    // Fallback: map file directory
    if (!currentMapFileName.isEmpty())
    {
        return QFileInfo(currentMapFileName).absolutePath();
    }

    return QString();
}

void MapEditorWindow::setupFloatingToolBar()
{
    floatingToolBar = new QFrame(this);
    floatingToolBar->setFrameStyle(QFrame::NoFrame);
    floatingToolBar->setAttribute(Qt::WA_TranslucentBackground, true);
    floatingToolBar->setStyleSheet(
        "QFrame { background: rgba(40, 40, 40, 180); border: 1px solid rgba(100, 100, 100, 200); border-radius: 6px; }"
        "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 3px; margin: 1px; }"
        "QToolButton:hover { background: rgba(80, 80, 80, 150); border: 1px solid rgba(140, 140, 140, 180); }"
        "QToolButton:checked { background: rgba(60, 120, 200, 160); border: 1px solid rgba(100, 160, 240, 200); }");
    floatingToolBar->setFixedSize(42, 0);

    QVBoxLayout* layout = new QVBoxLayout(floatingToolBar);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(1);

    struct ToolBinding
    {
        QAction* action;
    };

    QList<ToolBinding> editTools = {
        { ui->actionToolSelect },
        { ui->actionToolPan },
        { ui->actionToolAreaSelect },
        { ui->actionToolObstaclePaint },
        { ui->actionToolTrapPaint },
        { ui->actionToolNpcPlace },
        { ui->actionToolObjectPlace },
    };

    for (const auto& tool : editTools)
    {
        QToolButton* btn = new QToolButton(floatingToolBar);
        btn->setDefaultAction(tool.action);
        btn->setFixedSize(36, 28);
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        layout->addWidget(btn);
    }

    int toolCount = editTools.size();
    floatingToolBar->setFixedSize(42, toolCount * 29 + 8);

    repositionFloatingToolBar();
    floatingToolBar->raise();
    floatingToolBar->show();
}

void MapEditorWindow::repositionFloatingToolBar()
{
    if (!floatingToolBar || !canvas)
        return;

    // 将画布左上角映射到顶层窗口坐标，工具条相对画布左上角偏移 (4, 4)
    QPoint topLeft = canvas->mapTo(this, QPoint(0, 0));
    int x = topLeft.x() + 4;
    int y = topLeft.y() + 4;
    floatingToolBar->move(x, y);
}

void MapEditorWindow::syncToolActionFromCanvas(MapEditTool tool)
{
    QAction* action = nullptr;
    QString statusText;

    switch (tool)
    {
    case MapEditTool::Select:
        action = ui->actionToolSelect;
        statusText = tr("选择");
        break;
    case MapEditTool::TilePaint:
        action = ui->actionToolTilePaint;
        statusText = tr("瓦片绘制");
        break;
    case MapEditTool::ObstaclePaint:
        action = ui->actionToolObstaclePaint;
        statusText = tr("障碍绘制");
        break;
    case MapEditTool::TrapPaint:
        action = ui->actionToolTrapPaint;
        statusText = tr("陷阱绘制");
        break;
    case MapEditTool::NpcPlace:
        action = ui->actionToolNpcPlace;
        statusText = tr("NPC放置");
        break;
    case MapEditTool::ObjectPlace:
        action = ui->actionToolObjectPlace;
        statusText = tr("物体放置");
        break;
    case MapEditTool::TilePicker:
        action = ui->actionToolTilePicker;
        statusText = tr("吸管工具");
        break;
    case MapEditTool::AreaSelect:
        action = ui->actionToolAreaSelect;
        statusText = tr("区域选择");
        break;
    case MapEditTool::Pan:
        action = ui->actionToolPan;
        statusText = tr("拖拽地图");
        break;
    }

    if (action)
        action->setChecked(true);
    if (statusToolLabel)
        statusToolLabel->setText(statusText);
    showTaskPageForTool(tool);
}

void MapEditorWindow::showTaskPageForTool(MapEditTool tool)
{
    if (!taskPanel)
        return;

    switch (tool)
    {
    case MapEditTool::TilePaint:
    case MapEditTool::ObstaclePaint:
    case MapEditTool::TrapPaint:
    case MapEditTool::TilePicker:
    case MapEditTool::AreaSelect:
        updatePaintSettingsForTool(tool);
        taskPanel->setCurrentWidget(paintTaskPage);
        break;
    case MapEditTool::NpcPlace:
        if (entityTabs)
            entityTabs->setCurrentIndex(0);
        taskPanel->setCurrentWidget(entityTaskPage);
        break;
    case MapEditTool::ObjectPlace:
        if (entityTabs)
            entityTabs->setCurrentIndex(1);
        taskPanel->setCurrentWidget(entityTaskPage);
        break;
    case MapEditTool::Select:
        if (canvas && canvas->getSelectedEntityIndex() >= 0)
        {
            if (entityTabs)
                entityTabs->setCurrentIndex(canvas->isSelectedEntityNpc() ? 0 : 1);
            taskPanel->setCurrentWidget(entityTaskPage);
        }
        else
        {
            taskPanel->setCurrentWidget(inspectTaskPage);
        }
        break;
    case MapEditTool::Pan:
        taskPanel->setCurrentWidget(inspectTaskPage);
        break;
    }
}

void MapEditorWindow::updatePaintSettingsForTool(MapEditTool tool)
{
    if (!paintSettingsLayout)
        return;

    const bool tilePaint = tool == MapEditTool::TilePaint ||
        tool == MapEditTool::TilePicker;
    const bool areaSelect = tool == MapEditTool::AreaSelect;
    const bool obstaclePaint = tool == MapEditTool::ObstaclePaint;
    const bool trapPaint = tool == MapEditTool::TrapPaint;

    paintSettingsLayout->setRowVisible(
        paintLayerCombo, tilePaint || areaSelect);
    paintSettingsLayout->setRowVisible(
        rectangularAreaSelectCheck, areaSelect);
    paintSettingsLayout->setRowVisible(paintMpcRowWidget, tilePaint);
    paintSettingsLayout->setRowVisible(frameSpinBox, tilePaint);
    paintSettingsLayout->setRowVisible(obstacleComboBox, obstaclePaint);
    paintSettingsLayout->setRowVisible(trapIndexSpinBox, trapPaint);
    paintSettingsLayout->setRowVisible(tilePreviewLabel, tilePaint);
    paintSettingsLayout->setRowVisible(framePreviewScrollArea, tilePaint);
}

bool MapEditorWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == canvas)
    {
        if (event->type() == QEvent::Resize)
        {
            repositionFloatingToolBar();
        }
        else if (event->type() == QEvent::Leave)
        {
            // Mouse left canvas — clear coordinate display
            if (statusCoordLabel)
                statusCoordLabel->setText("X:- Y:-");
        }
    }
    else if (watched == framePreviewScrollArea)
    {
        if (event->type() == QEvent::Resize && framePreviewCurrentMpcIndex > 0)
        {
            // 滚动区域尺寸变化时重新布局帧网格
            updateFramePreviewGrid(framePreviewCurrentMpcIndex);
        }
    }
    // tilePreviewLabel 是 MpcPreviewLabel，自带 resize 适配逻辑，无需在这里处理。
    return QWidget::eventFilter(watched, event);
}

void MapEditorWindow::onMpcSelectionChanged(int index)
{
    if (index < 0)
        return;

    int mpcIndex = mpcComboBox->itemData(index).toInt();
    canvas->setPaintMpcIndex(mpcIndex);
    updateFrameSpinBox();
    updateFramePreviewGrid(mpcIndex);

    if (mpcIndex <= 0)
    {
        frameSpinBox->setEnabled(false);
    }
    else
    {
        frameSpinBox->setEnabled(true);
    }
}

void MapEditorWindow::onFillSelectedArea()
{
    fillSelectedArea();
}

void MapEditorWindow::onClearSelectedArea()
{
    clearSelectedArea();
}

void MapEditorWindow::onLocateSelectedEntity()
{
    centerOnSelectedEntity();
}

void MapEditorWindow::updateFramePreviewGrid(int mpcIndex)
{
    if (!framePreviewGrid)
        return;

    // Clear old buttons
    QLayoutItem* item;
    while ((item = framePreviewGrid->takeAt(0)) != nullptr)
    {
        delete item->widget();
        delete item;
    }

    if (mpcIndex <= 0 || !mapEditor.isLoaded())
        return;

    // mpcIndex is stored value (1-based), convert to 0-based slot
    int slotIndex = mpcIndex - 1;
    if (slotIndex < 0 || slotIndex >= MAP_EDITOR_MPC_COUNT)
        return;

    std::string mpcPath = mapEditor.getMpcFilePath(slotIndex);
    int frameCount = mpcCache.getFrameCount(mpcPath);
    if (frameCount <= 0)
        return;

    // 动态计算帧预览卡片尺寸和列数，根据滚动区域视口宽度自适应
    const int checkerSize = 8;
    const int minBtnW = 64;
    const int minBtnH = 48;
    const int maxBtnW = 160;
    const int maxBtnH = 120;
    const int gridSpacing = 2;
    const int gridMargin = 4;

    int availWidth = framePreviewScrollArea->viewport()->width() - gridMargin * 2;
    if (availWidth < minBtnW)
        availWidth = minBtnW;

    // 每个按钮宽度：尽量大，但不超过 maxBtnW，至少 minBtnW
    int previewBtnW = std::min(maxBtnW, std::max(minBtnW, (availWidth + gridSpacing) / std::max(1, availWidth / minBtnW) - gridSpacing));
    int cols = std::max(1, (availWidth + gridSpacing) / (previewBtnW + gridSpacing));
    // 根据列数重新精确计算按钮宽度，填满可用空间
    previewBtnW = std::min(maxBtnW, (availWidth - (cols - 1) * gridSpacing) / cols);
    int previewBtnH = std::min(maxBtnH, std::max(minBtnH, previewBtnW * 3 / 4));

    for (int f = 0; f < frameCount && f < 256; f++)
    {
        QImage frameImg = mpcCache.getFrameImage(mpcPath, f);

        // 即使帧图片为空也要创建按钮（保证 Frame 0 可点击），显示帧编号
        QPixmap checkerBg(previewBtnW, previewBtnH);
        checkerBg.fill(Qt::transparent);
        QPainter checkerPainter(&checkerBg);
        checkerPainter.fillRect(0, 0, previewBtnW, previewBtnH, QColor(255, 255, 255));
        for (int cy = 0; cy < previewBtnH; cy += checkerSize)
        {
            for (int cx = 0; cx < previewBtnW; cx += checkerSize)
            {
                if (((cx / checkerSize) + (cy / checkerSize)) % 2 == 0)
                    checkerPainter.fillRect(cx, cy, checkerSize, checkerSize, QColor(204, 204, 204));
            }
        }

        if (!frameImg.isNull())
        {
            // Scale frame image to fit the button while maintaining aspect ratio
            QPixmap frame = QPixmap::fromImage(frameImg.scaled(
                previewBtnW - 4, previewBtnH - 4,
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
            checkerPainter.drawPixmap(
                (previewBtnW - frame.width()) / 2,
                (previewBtnH - frame.height()) / 2,
                frame);
        }
        // 在右下角绘制帧编号
        checkerPainter.setPen(QColor(0, 0, 0, 180));
        checkerPainter.setFont(QFont("Arial", 7));
        checkerPainter.drawText(previewBtnW - 22, previewBtnH - 3, QString::number(f));
        checkerPainter.end();

        QPushButton* btn = new QPushButton();
        btn->setFixedSize(previewBtnW, previewBtnH);
        btn->setIconSize(QSize(previewBtnW - 4, previewBtnH - 4));
        btn->setIcon(QIcon(checkerBg));
        btn->setToolTip(tr("帧 %1").arg(f));
        btn->setCheckable(true);
        if (f == frameSpinBox->value())
            btn->setChecked(true);

        int row = f / cols;
        int col = f % cols;
        framePreviewGrid->addWidget(btn, row, col);

        connect(btn, &QPushButton::clicked, this, [this, f]() {
            frameSpinBox->setValue(f);
        });
    }

    framePreviewCurrentMpcIndex = mpcIndex;
    framePreviewCurrentCols = cols;
}

void MapEditorWindow::syncPaintUIFromPick(int storedMpc, int frameIndex, int layer)
{
    for (int i = 0; i < mpcComboBox->count(); i++)
    {
        if (mpcComboBox->itemData(i).toInt() == storedMpc)
        {
            mpcComboBox->setCurrentIndex(i);
            break;
        }
    }
    frameSpinBox->setValue(frameIndex);
    if (layer >= 0 && layer < 3)
    {
        for (int i = 0; i < paintLayerCombo->count(); i++)
        {
            if (paintLayerCombo->itemData(i).toInt() == layer)
            {
                paintLayerCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

void MapEditorWindow::syncPaintUIFromAllLayersPick(const MapTileData& tileData)
{
    // 用最高非空层作为预览；三层都空时取地面层（mpc=0）。
    int previewLayer = 0;
    for (int layer = 2; layer >= 0; layer--)
    {
        if (tileData.layer[layer].mpc != 0)
        {
            previewLayer = layer;
            break;
        }
    }
    MapTileLayerData preview = tileData.layer[previewLayer];

    // 同步 MPC/frame 时阻塞信号：onMpcSelectionChanged/onFrameSelectionChanged 会
    // 调用 canvas->setPaintMpcIndex/setPaintFrameIndex，进而清除多层画笔状态。
    // 多层画笔已由 canvas 在发射信号前保存，这里只是更新预览控件，不应破坏它。
    {
        QSignalBlocker mpcBlocker(mpcComboBox);
        for (int i = 0; i < mpcComboBox->count(); i++)
        {
            if (mpcComboBox->itemData(i).toInt() == preview.mpc)
            {
                mpcComboBox->setCurrentIndex(i);
                break;
            }
        }
    }
    {
        QSignalBlocker frameBlocker(frameSpinBox);
        // updateFrameSpinBox 会按 MPC 的帧数 setRange；若 preview.frame 超出新范围，
        // setRange 会把 value 夹到范围内并发出 valueChanged，进而触发
        // onFrameSelectionChanged -> canvas->setPaintFrameIndex -> 清除多层画笔。
        // 多层画笔已由 canvas 保存，这里只是同步预览控件，继续阻塞信号保护它。
        updateFrameSpinBox();
        frameSpinBox->setValue(preview.frame);
    }
    updateFramePreviewGrid(preview.mpc);
    updateTilePreview();

    // 图层下拉框保持“全部图层”，不切回单层（QSignalBlocker 防止误触发
    // onPaintLayerChanged 调用 setPaintLayer 而清除多层画笔）。这里显式确保停在全部图层。
    {
        QSignalBlocker layerBlocker(paintLayerCombo);
        for (int i = 0; i < paintLayerCombo->count(); i++)
        {
            if (paintLayerCombo->itemData(i).toInt() == -1)
            {
                paintLayerCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    // canvas 状态必须确实处于全部图层（即使 combo 没变化，也保证一致性）。
    canvas->setPaintAllLayers(true);

    if (statusToolLabel)
    {
        statusToolLabel->setText(
            tr("已拾取全部图层：地面 %1/%2，建筑 %3/%4，空中 %5/%6")
                .arg(tileData.layer[0].mpc).arg(tileData.layer[0].frame)
                .arg(tileData.layer[1].mpc).arg(tileData.layer[1].frame)
                .arg(tileData.layer[2].mpc).arg(tileData.layer[2].frame));
    }
}

void MapEditorWindow::updateEntityTabs()
{
    if (!entityTabs)
        return;

    // NPC tab
    QString npcTabText;
    if (!isNpcListOpen)
    {
        npcTabText = tr("NPC (未加载)");
    }
    else
    {
        int count = (int)canvas->getNpcList().size();
        npcTabText = tr("NPC (%1)").arg(count);
        if (isNpcListModified)
            npcTabText += QStringLiteral("*");
    }
    entityTabs->setTabText(0, npcTabText);

    // Object tab
    QString objTabText;
    if (!isObjectListOpen)
    {
        objTabText = tr("物体 (未加载)");
    }
    else
    {
        int count = (int)canvas->getObjectList().size();
        objTabText = tr("物体 (%1)").arg(count);
        if (isObjectListModified)
            objTabText += QStringLiteral("*");
    }
    entityTabs->setTabText(1, objTabText);

    // File path label
    if (entityFilePathLabel)
    {
        QString npcInfo;
        if (!isNpcListOpen)
        {
            npcInfo = tr("NPC: 未加载");
        }
        else
        {
            QString npcFile = currentNpcFileName.isEmpty() ?
                tr("(新建)") : QFileInfo(currentNpcFileName).fileName();
            npcInfo = tr("NPC: %1%2").arg(npcFile)
                .arg(isNpcListModified ? QStringLiteral(" *") : QString());
        }

        QString objInfo;
        if (!isObjectListOpen)
        {
            objInfo = tr("物体: 未加载");
        }
        else
        {
            QString objFile = currentObjectFileName.isEmpty() ?
                tr("(新建)") : QFileInfo(currentObjectFileName).fileName();
            objInfo = tr("物体: %1%2").arg(objFile)
                .arg(isObjectListModified ? QStringLiteral(" *") : QString());
        }

        entityFilePathLabel->setText(npcInfo + QStringLiteral(" | ") + objInfo);
    }
}

void MapEditorWindow::rebuildEntityList(QListWidget* list, const std::vector<MapEntityData>& entities,
                                         const QString& filter, bool isNpc)
{
    Q_UNUSED(isNpc);
    if (!list)
        return;
    list->clear();
    for (size_t i = 0; i < entities.size(); i++)
    {
        QString name = QString::fromUtf8(entities[i].name.c_str());
        if (!filter.isEmpty() && !name.toLower().contains(filter))
            continue;
        QString text = QString("[%1] %2 (%3,%4)")
            .arg(i)
            .arg(name)
            .arg(entities[i].mapX)
            .arg(entities[i].mapY);
        QListWidgetItem* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, (int)i);
        list->addItem(item);
    }
}

void MapEditorWindow::selectListRowByEntityIndex(QListWidget* list, int entityIndex)
{
    if (!list)
        return;
    for (int i = 0; i < list->count(); i++)
    {
        QListWidgetItem* item = list->item(i);
        if (item && item->data(Qt::UserRole).toInt() == entityIndex)
        {
            list->setCurrentItem(item);
            return;
        }
    }
    list->clearSelection();
}

void MapEditorWindow::updateListWidgetItemCoords(QListWidget* list, int entityIndex, const MapEntityData& entity)
{
    if (!list)
        return;
    for (int i = 0; i < list->count(); i++)
    {
        QListWidgetItem* item = list->item(i);
        if (item && item->data(Qt::UserRole).toInt() == entityIndex)
        {
            QString name = QString::fromUtf8(entity.name.c_str());
            item->setText(QString("[%1] %2 (%3,%4)")
                .arg(entityIndex)
                .arg(name)
                .arg(entity.mapX)
                .arg(entity.mapY));
            return;
        }
    }
}

void MapEditorWindow::centerOnSelectedEntity()
{
    MapEntityData* entity = canvas->getSelectedEntity();
    if (!entity)
        return;
    canvas->centerOnTile(entity->mapX, entity->mapY);
    canvas->update();
}

void MapEditorWindow::syncListSelectionForEntity(int index, bool isNpc)
{
    if (syncingEntitySelection)
        return;
    syncingEntitySelection = true;
    QListWidget* list = isNpc ? npcListWidget : objectListWidget;
    selectListRowByEntityIndex(list, index);
    syncingEntitySelection = false;
}
