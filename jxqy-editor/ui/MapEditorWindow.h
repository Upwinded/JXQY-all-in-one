#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QListWidget>
#include <QList>
#include <QTreeWidget>
#include <QSplitter>
#include <QByteArray>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include "../core/MapFileEditor.h"
#include "../core/MpcImageCache.h"
#include "../core/INIFileEditor.h"
#include "../core/DesktopRunDocumentSnapshot.h"
#include "../core/ProjectDocumentRegistry.h"
#include "UndoRedoManager.h"
#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"

class MapRenderCanvas;
class MinimapWidget;
class MpcPreviewLabel;
class DurableFileTransaction;
class QCheckBox;
class QLineEdit;
class QScrollArea;
class QGridLayout;
class QFormLayout;
class QFrame;
class QTabWidget;
class QPushButton;
class QEvent;

namespace Ui
{
class MapEditorWindow;
}

struct CaseInsensitiveQStringLess
{
    bool operator()(const QString& left, const QString& right) const
    {
        return QString::compare(left, right, Qt::CaseInsensitive) < 0;
    }
};

class MapEditorWindow : public QWidget,
                        public AssetsPathSwitchParticipant,
                        public CloseTransactionParticipant
{
    Q_OBJECT

public:
    struct EditingViewState
    {
        double zoomLevel = 1.0;
        int scrollX = 0;
        int scrollY = 0;
    };

    explicit MapEditorWindow(QWidget* parent = nullptr);
    ~MapEditorWindow();
    bool setAssetsBasePath(const QString& path);
    QString getAssetsBasePath() const;

    Decision prepareAssetsPathSwitch(const QString& path) const override;
    bool resolveAssetsPathSwitch(Decision decision) override;
    void commitAssetsPathSwitch(const QString& path) override;
    QString currentAssetsPath() const override;

    ClosePlan prepareCloseTransaction() const override;
    bool resolveCloseTransaction(const ClosePlan& plan) override;
    void commitCloseTransaction(const ClosePlan& plan) override;

    bool openMapFile(const QString& fileName);
    bool saveMapFile(const QString& fileName);
    bool saveMapFile();
    bool createNewMap(int width, int height);
    EditingViewState editingViewState() const;
    bool restoreEditingViewState(const EditingViewState& state);

    bool loadNpcListFromFile(const QString& fileName, bool confirmReplacement = true);
    bool loadObjectListFromFile(const QString& fileName, bool confirmReplacement = true);
    bool saveNpcListToFile(const QString& fileName) const;
    bool saveObjectListToFile(const QString& fileName) const;
    bool saveNpcListAsFile(const QString& fileName);
    bool saveObjectListAsFile(const QString& fileName);

    struct ProjectListRestoreResult
    {
        bool npcListRestored = true;
        bool objectListRestored = true;
    };
    ProjectListRestoreResult restoreProjectListDocuments(
        const QString& npcListFileName,
        const QString& objectListFileName);

    struct DesktopRunSnapshotBundle
    {
        bool mapLoaded = false;
        QString assetsBasePath;
        QString currentMapFilePath;
        QString currentNpcFilePath;
        QString currentObjectFilePath;
        bool npcListOpen = false;
        bool objectListOpen = false;
        int mapWidth = 0;
        int mapHeight = 0;
        QList<DesktopRunDocumentSnapshot> documents;
    };
    DesktopRunSnapshotBundle desktopRunSnapshotBundle() const;
    // Saved-scene/current-script collection avoids serializing clean MAP,
    // NPC, and OBJ documents from unrelated open map windows. Referenced
    // pending MPC bytes still carry owner-MAP provenance for locked filtering.
    QList<DesktopRunDocumentSnapshot>
        desktopRunGenericDocumentSnapshots() const;

    QList<ProjectDocumentState> currentProjectDocuments() const;
    using DocumentPathValidator =
        std::function<bool(const QString& currentPath,
                           const QString& targetPath)>;
    void setDocumentPathValidator(DocumentPathValidator validator)
    {
        documentPathValidator = std::move(validator);
    }

    /// 测试与脚本访问地图数据编辑器（只读视图）。调用方不得持有指针超出窗口生命周期。
    const MapFileEditor& getMapEditor() const { return mapEditor; }
    /// 测试用可写访问（设置瓦片引用等）。生产代码不应使用。
    MapFileEditor& getMapEditorRef() { return mapEditor; }
    /// 测试用：外部直接修改 MPC 表后重建下拉框并刷新主预览，模拟用户选择槽位。
    void refreshMpcPreviewForTest()
    {
        updateMpcComboBox();
        updateTilePreview();
    }

signals:
    void documentStatesChanged();
    void documentClosed();

private slots:
    void onOpenMap();
    void onNewMap();
    void onSaveMap();
    void onSaveMapAs();
    void onExportThumbnail();
    void onExportBmp();

    void onToolSelect();
    void onToolTilePaint();
    void onToolObstaclePaint();
    void onToolTrapPaint();
    void onToolNpcPlace();
    void onToolObjectPlace();

    void onUndo();
    void onRedo();

    void onTileClicked(int tileX, int tileY, Qt::MouseButton button);
    void onTileHovered(int tileX, int tileY);
    void onEntitySelected(int index, bool isNpc);
    void onEntityDoubleClicked(int index, bool isNpc);
    void onEntityMoved(int index, bool isNpc, int newTileX, int newTileY);
    void onEntityListChanged();
    void onTileEdited(int tileX, int tileY);
    void onTileAboutToBeEdited(int tileX, int tileY);
    void onEntityPlaced(int index, bool isNpc);
    void onEntityMoveStarted(int index, bool isNpc, int oldMapX, int oldMapY);
    void onEntityDeleteRequested();
    void onZoomChanged(float zoomLevel);

    void onLayerVisibilityChanged(int layer, bool visible);
    void onObstacleVisibilityChanged(bool visible);
    void onTrapVisibilityChanged(bool visible);
    void onNpcVisibilityChanged(bool visible);
    void onObjectVisibilityChanged(bool visible);
    void onGridVisibilityChanged(bool visible);

    void onPaintLayerChanged(int index);
    void onMpcSelectionChanged(int index);
    void onFrameSelectionChanged(int value);
    void onObstacleTypeChanged(int index);
    void onTrapIndexChanged(int value);

    void onNpcListSelectionChanged();
    void onObjectListSelectionChanged();
    void onAddNpc();
    void onAddObject();
    void onDeleteEntity();
    void onEditEntityProperties(int index, bool isNpc);
    void onLocateSelectedEntity();

    void onUndoStackChanged();

    void onPickTileFromMap(int tileX, int tileY);
    void onEditMpcInfo();
    void onEditTrapScripts();
    void onCopyArea();
    void onPasteArea();
    void onGotoTile();
    void onDuplicateEntity();
    void onSelectAll();
    void onMapStatistics();
    void onResizeMap();
    void onClearAllObstacles();
    void onClearAllTraps();
    void onFillSelectedArea();
    void onClearSelectedArea();
    void onZoomToFit();
    void onCoordinateVisibilityChanged(bool visible);
    void onSaveNpcList();
    void onSaveNpcListAs();
    void onCloseNpcList();
    void onSaveObjectList();
    void onSaveObjectListAs();
    void onCloseObjectList();

    // 左侧 MPC 索引下拉框旁的快速增删改入口（与菜单 MPC 管理器共用业务逻辑）。
    void onAddMpcSlot();
    void onEditMpcSlot();
    void onDeleteMpcSlot();

private slots:
    void onCloseRequested();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QList<DesktopRunDocumentSnapshot>
        captureDesktopRunMapDocuments(
            bool includeCleanDirectDocuments,
            bool pinRuntimeSaveCompanions) const;
    void setupToolbar();
    void setupConnections();
    /// 构建顶部菜单栏（文件/编辑/视图/工具/列表/帮助），承接原画布右键菜单项。
    void setupMenuBar();
    /// 集中刷新动态创建的菜单、动作、状态栏工具名和实体列表标签。
    /// ui->retranslateUi() 只刷新 .ui 内静态控件，本函数负责 setupMenuBar/setupConnections
    /// 中 new 出来的菜单项与动作，使已打开的地图编辑器在切换语言时即时刷新。
    void retranslateDynamicUi();

    /// 构建覆盖在画布左上角的浮动编辑工具条，并把编辑工具从顶部 toolbar 移出。
    void setupFloatingToolBar();
    /// 根据画布当前尺寸重定位浮动工具条到左上角。
    void repositionFloatingToolBar();
    /// 画布编辑工具变化（含放置完成自动切回选择）时，同步 QAction 选中态与状态栏。
    void syncToolActionFromCanvas(MapEditTool tool);
    /// 只显示当前地图任务需要的侧栏页面，并同步该工具对应的精简设置行。
    void showTaskPageForTool(MapEditTool tool);
    void updatePaintSettingsForTool(MapEditTool tool);
    /// 填充当前区域选区。单图层写 paintLayer；全部图层+多层画笔按 layer[0..2] 写回；
    /// 全部图层+标量画笔写三层。每次生成一条撤销记录。
    void fillSelectedArea();
    /// 清除当前区域选区。单图层清 paintLayer；全部图层清 0/1/2 三层。
    /// 保留 obstacle/trap。每次生成一条撤销记录。
    void clearSelectedArea();

    void updateMpcComboBox();
    void updateFrameSpinBox();
    void updateTilePreview();
    void updateFramePreviewGrid(int mpcIndex);
    void updateTileInfoPanel(int tileX, int tileY, bool isSelected = false);
    void refreshCurrentTileInfo();
    void syncPaintUIFromPick(int storedMpc, int frameIndex, int layer);
    /// 全部图层模式下左键拾取的多层 tile 同步：把最高非空层（无则地面层）作为
    /// MPC/frame 预览显示，但保持图层下拉框在“全部图层”，并给出状态栏提示。
    void syncPaintUIFromAllLayersPick(const MapTileData& tileData);
    void updateEntityInfoPanel(int index, bool isNpc);
    void updateNpcListWidget();
    void updateObjectListWidget();
    void updateEntityCountStatus();

    // ---- MPC 索引增删改共享业务逻辑 ----
    // 刷新 MPC 相关 UI：记录当前选择 → 清缓存 → 重建下拉框/帧数据 → 按需要恢复选择。
    // 这是左侧按钮和菜单管理器共用的唯一刷新入口。
    bool refreshMpcUi(int restoreStoredMpcIndex = -1);

public:
    /// 可注入的文件选择服务：默认实现调用 QFileDialog。测试可注入假实现驱动
    /// 真实业务入口（onAddMpcSlot/onEditMpcSlot/菜单管理器），无需依赖真实文件对话框。
    struct MpcFileSelection
    {
        // 返回用户选择的源文件绝对路径；用户取消返回空。
        std::function<QString()> pickSourceFile;
    };
    // 注入文件选择服务（用于测试）。传默认构造的 MpcFileSelection 恢复 QFileDialog 行为。
    void setMpcFileSelectionForTest(const MpcFileSelection& selection) { mpcFileSelection = selection; }
    // 返回当前是否注入了测试文件选择器。
    bool hasInjectedMpcFileSelection() const { return mpcFileSelection.pickSourceFile != nullptr; }

private:
    MpcFileSelection mpcFileSelection;
    std::map<QString, QByteArray, CaseInsensitiveQStringLess> pendingCreatedMpcResources;
    bool lastMpcResourceSyncSucceeded = true;
    bool suppressMpcResourceSync = false;

public:
    // 统计三个 Tile 图层中对指定 0-based 槽位的引用数量（Tile.mpc 是 1-based，需 +1）。
    int countMpcSlotReferences(int slotIndex) const;
    // 校验 MPC 名称（非空、不重复、UTF-8 字节长度 <= 31）。excludeSlots 为编辑多槽位时
    // 需要排除的槽位集合（包含待校验槽位本身）。校验失败时通过 QMessageBox 提示并返回 false。
    bool validateMpcName(const QString& name, int excludeSlot,
                         const std::set<int>& extraExcludeSlots = {}) const;

private:
    // 计算当前地图实际 MPC 解析目录（相对 assets 根），与 getMpcFilePath 一致：
    //   头部 mpcPath 非空 → mpcPath（补齐分隔符）；
    //   否则 → mpc/map/<mapFileName-base>/（mapFileName 为空时为 mpc/map/）。
    // 返回相对路径（如 "mpc/map/mymap/"），始终以分隔符结尾。
    QString resolveManagedMpcDirRelative() const;
    // 解析目录的绝对路径（assetsBasePath + resolveManagedMpcDirRelative）。
    QString resolveManagedMpcDirAbsolute() const;
    // 在真正读写前重新解析并验证文件名、assets containment 与当前 MPC 目录。
    // 解析失败时返回空，调用方不得把空 QDir 当作当前工作目录继续使用。
    QString resolveManagedMpcTargetPath(const QString& mpcName) const;
    bool isPendingMpcPathSafe(const QString& absolutePath) const;
    // 用户选择源 MPC 文件，返回 {绝对源路径, 文件名}；取消返回空 pair。
    // 默认用 QFileDialog，注入的 mpcFileSelection 优先。
    QPair<QString, QString> pickSourceMpcFile();
    // 准备资源提交：验证源文件并判断目标是复用、冲突还是需要原子复制。
    // 同名文件已存在时：内容相同则直接复用（返回现有文件路径，标记 reuseExisting=true）；
    // 内容不同则提示用户（覆盖/改名/取消），返回相应结果。
    enum class StageResult { Ok, Conflict, Cancelled, Failed };
    StageResult prepareMpcResource(const QString& sourceAbsPath, const QString& mpcName,
                                   QString& sourcePathOut, bool& reuseExistingOut);
    // 使用 QSaveFile 原子复制到最终目标；内容相同的复用情况为空操作。
    bool commitMpcResource(const QString& sourceAbsPath, const QString& finalAbsPath, bool reuseExisting);
    bool syncPendingMpcResourcesWithTable(DurableFileTransaction* transaction = nullptr);
    void finalizePendingMpcResourcesAfterSave();
    void discardPendingMpcResources(bool showWarnings = true);

    // 核心提交：根据完整 255 槽位的新表，计算哪些槽位被清空（原非空→新空），
    // 收集这些槽位的 Tile 引用清理数据，构建单个 MpcInfoEditCommand，写入 redo 状态并 push。
    // 用于左侧单槽编辑和菜单多槽编辑共用。restoreStoredMpcIndex 指定刷新后恢复的选择。
    void commitMpcTableChange(const MpcInfoData (&newMpc)[MAP_EDITOR_MPC_COUNT],
                              int restoreStoredMpcIndex,
                              const QString& description);

public:
    // 将槽位 slotIndex 的 MPC 信息替换为 newInfo（保持槽位号不变），生成旧/新表快照。
    bool applyMpcEdit(int slotIndex, const MpcInfoData& newInfo,
                      MpcInfoData (&oldMpcOut)[MAP_EDITOR_MPC_COUNT],
                      MpcInfoData (&newMpcOut)[MAP_EDITOR_MPC_COUNT]) const;
    // 给定新表，找出所有"原非空→新空"的槽位，收集这些槽位的 Tile 引用清理数据。
    // 返回清理引用总数。oldMpcOut 为当前表，newMpcOut 直接使用调用方传入的 newTable。
    int collectReferenceCleanup(const MpcInfoData (&newTable)[MAP_EDITOR_MPC_COUNT],
                                MpcInfoData (&oldMpcOut)[MAP_EDITOR_MPC_COUNT],
                                std::map<std::pair<int,int>, MapTileData>& oldTilesOut,
                                std::map<std::pair<int,int>, MapTileData>& newTilesOut) const;

private:
    // 同步 NPC/OBJ Tab 标签与 tooltip（含数量与当前列表文件名），让加载/保存目标可见。
    void updateEntityTabs();
    // 重建实体列表：按过滤词匹配名称/资源/脚本，每项展示名称、坐标与资源/脚本摘要。
    void rebuildEntityList(QListWidget* list, const std::vector<MapEntityData>& entities,
                           const QString& filter, bool isNpc);

    // 实体列表 / 地图画布 / 属性面板三者联动辅助。
    void selectListRowByEntityIndex(QListWidget* list, int entityIndex);
    void updateListWidgetItemCoords(QListWidget* list, int entityIndex, const MapEntityData& entity);
    void centerOnSelectedEntity();
    // 将列表选中态与属性面板同步到指定实体（画布选中态的镜像）。内部带保存/恢复的
    // 同步守卫，可被 onEntitySelected 等在画布联动回调中安全调用。
    void syncListSelectionForEntity(int index, bool isNpc);

    void setModified(bool modified);
    void syncDirtyStateFromUndoHistory();
    bool canAdoptDocumentPath(const QString& currentPath,
                              const QString& targetPath) const;
    bool hasDocumentTargetConflict(ProjectDocumentType documentType,
                                   const QString& targetPath) const;
    void closeNpcListDocument();
    void closeObjectListDocument();
    void resetUndoDomain(UndoDomain domain);
    void updateWindowTitle();
    QString getDefaultDirectoryForList(bool isNpc) const;

    enum class SaveConfirmResult { Saved, Discarded, Cancelled };
    SaveConfirmResult confirmSaveIfModified();

    Ui::MapEditorWindow* ui;

    MapFileEditor mapEditor;
    MpcImageCache mpcCache;
    MapRenderCanvas* canvas = nullptr;
    MinimapWidget* minimapWidget = nullptr;
    UndoRedoManager undoRedoManager;

    QString assetsBasePath;
    QString currentMapFileName;
    QString currentNpcFileName;
    QString currentObjectFileName;
    bool isModified = false;

    // NPC/OBJ 列表独立状态追踪
    bool isNpcListOpen = false;       // NPC 列表是否已打开/加载
    bool isObjectListOpen = false;    // OBJ 列表是否已打开/加载
    bool npcListLoadedFromRuntimeSave = false;
    bool objectListLoadedFromRuntimeSave = false;
    bool isNpcListModified = false;   // NPC 列表是否有未保存修改
    bool isObjectListModified = false;// OBJ 列表是否有未保存修改
    bool saveNpcWithMap = false;      // 保存地图时是否同时保存 NPC 列表
    bool saveObjWithMap = false;      // 保存地图时是否同时保存 OBJ 列表
    DocumentPathValidator documentPathValidator;

    QLabel* statusCoordLabel = nullptr;
    QLabel* statusZoomLabel = nullptr;
    QLabel* statusMapSizeLabel = nullptr;
    QLabel* statusToolLabel = nullptr;
    QLabel* statusEntityCountLabel = nullptr;

    QComboBox* mpcComboBox = nullptr;
    QComboBox* paintLayerCombo = nullptr;
    QSpinBox* frameSpinBox = nullptr;
    QComboBox* obstacleComboBox = nullptr;
    QSpinBox* trapIndexSpinBox = nullptr;
    QFormLayout* paintSettingsLayout = nullptr;
    QWidget* paintMpcRowWidget = nullptr;
    // 主预览控件使用自定义 MpcPreviewLabel：sizeHint 不受 pixmap 影响，避免
    // setPixmap→布局 Resize→重新缩放的尺寸反馈循环。
    MpcPreviewLabel* tilePreviewLabel = nullptr;
    QScrollArea* framePreviewScrollArea = nullptr;
    QGridLayout* framePreviewGrid = nullptr;
    int framePreviewCurrentMpcIndex = -1;
    int framePreviewCurrentCols = 1;

    // 右侧瓦片信息面板当前选中的瓦片坐标。
    // 点击瓦片时设置（形成稳定选中）；Esc 或画布清空选择时重置为 -1。
    // hover 只更新状态栏坐标，不覆盖选中瓦片信息。
    int selectedInfoTileX = -1;
    int selectedInfoTileY = -1;

    QListWidget* npcListWidget = nullptr;
    QListWidget* objectListWidget = nullptr;
    QTabWidget* taskPanel = nullptr;
    QWidget* paintTaskPage = nullptr;
    QWidget* entityTaskPage = nullptr;
    QWidget* inspectTaskPage = nullptr;
    QTabWidget* entityTabs = nullptr;
    QTreeWidget* tileInfoTree = nullptr;
    QTreeWidget* entityInfoTree = nullptr;
    QLabel* entityPreviewLabel = nullptr;
    QLineEdit* npcSearchEdit = nullptr;
    QLineEdit* objectSearchEdit = nullptr;
    QCheckBox* continuousPlaceCheck = nullptr;
    QCheckBox* saveNpcWithMapCheck = nullptr;
    QCheckBox* saveObjWithMapCheck = nullptr;
    QLabel* entityFilePathLabel = nullptr;
    QCheckBox* coordinateVisibleCheck = nullptr;
    // 区域选择形状：勾选使用方形，未勾选使用菱形（默认）。
    // 只影响下一次新选择，不改变已完成区域或剪贴板内容的形状。
    QCheckBox* rectangularAreaSelectCheck = nullptr;

    // 左侧 MPC 索引下拉框旁的快速增删改按钮（与菜单 MPC 管理器共用业务逻辑）。
    QPushButton* addMpcSlotButton = nullptr;
    QPushButton* editMpcSlotButton = nullptr;
    QPushButton* deleteMpcSlotButton = nullptr;

    QAction* actionUndo = nullptr;
    QAction* actionRedo = nullptr;

    // setupMenuBar 动态创建的菜单与动作。ui->retranslateUi() 不会刷新它们，
    // 由 retranslateDynamicUi() 在 LanguageChange 时集中重新翻译。
    QMenu* mapFileMenu = nullptr;
    QMenu* mapEditMenu = nullptr;
    QMenu* mapViewMenu = nullptr;
    QMenu* mapToolMenu = nullptr;
    QMenu* mapListMenu = nullptr;

    QAction* actionCopySelectedArea = nullptr;
    QAction* actionPasteArea = nullptr;
    QAction* actionClearClipboard = nullptr;
    QAction* actionFillSelectedArea = nullptr;
    QAction* actionClearSelectedArea = nullptr;
    QAction* actionClearAllObstacles = nullptr;
    QAction* actionClearAllTraps = nullptr;
    QAction* actionEditMpcInfo = nullptr;
    QAction* actionEditTrapScripts = nullptr;
    QAction* actionResizeMap = nullptr;
    QAction* actionSaveNpcList = nullptr;
    QAction* actionSaveNpcListAs = nullptr;
    QAction* actionCloseNpcList = nullptr;
    QAction* actionSaveObjectList = nullptr;
    QAction* actionSaveObjectListAs = nullptr;
    QAction* actionCloseObjectList = nullptr;

    QFrame* floatingToolBar = nullptr;

    MapTileData pendingOldTileData;
    bool hasPendingOldTileData = false;
    int pendingMoveEntityIndex = -1;
    bool pendingMoveEntityIsNpc = true;
    int pendingMoveOldMapX = 0;
    int pendingMoveOldMapY = 0;

    // 防止实体选择在 列表 <-> 画布 之间互相触发导致回环。
    bool syncingEntitySelection = false;

    void updateEntityPreview(const MapEntityData& entity);
    void clearRightInfoPanel();
};
