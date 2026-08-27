#pragma once

#include <QMainWindow>
#include <QAction>
#include <QMdiSubWindow>
#include <QLabel>
#include <QStringList>
#include <QDragEnterEvent>
#include <QHash>
#include <QPointer>
#include <QSet>
#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"
#include "ThemeManager.h"
#include "../core/GameProfile.h"
#include "../core/DesktopFileAssociationManager.h"
#include "../core/ProjectManager.h"
#include "../core/ProjectDocumentRegistry.h"
#include "../core/SavedSceneLaunchPreparation.h"

namespace Ui
{
class MainWindow;
}

class QActionGroup;
class QDockWidget;
class QMenu;
class QUndoStack;
class ProjectExplorerWidget;
class DesktopRunPanel;
class DesktopRunSessionCoordinator;
class ScriptEditorWindow;
class StoryGraphWindow;
class MapEditorWindow;
class MagicEditorWindow;
class GoodsShopEditorWindow;
class DialogueEditorWindow;
class NpcDataEditorWindow;
class MenuEditorWindow;
class ImageEditorWindow;
class BatchConvertWindow;
struct ProjectDocumentSessionState;
struct ProjectAssetMigrationContext;
struct StoryGraphNode;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    // Programmatic counterpart of the resources-directory action. The change
    // is committed only when every open editor accepts and resolves it.
    bool requestAssetsPathChange(const QString& path);
    bool requestActiveResourcePackChange(
        const QString& resourcePackId,
        const QString& resourcePackEntryKey = QString());
    bool createProject(const QString& filePath);
    bool createProject(
        const QString& filePath,
        const ProjectResourceConfiguration& resourceConfiguration);
    bool applyProjectResourceConfiguration(
        const ProjectResourceConfiguration& resourceConfiguration);
    bool openProject(const QString& filePath);
    bool openStartupFileArguments(const QStringList& fileArguments);
    void repairDesktopFileAssociations();
    // Called after QApplication::exec() returns. This performs only
    // non-blocking process/thread stop requests before process-exit policy is
    // evaluated.
    void prepareForApplicationExit();

protected:
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onProjectSettings();
    void onProjectRuntimeConfiguration();
    void onBatchConvert();
    void onExportAndroidExternalResource();
    void onOpenImageEditor();
    void onOpenMapEditor();
    void onOpenMenuEditor();
    void onOpenScriptEditor();
    void onOpenNpcDataEditor();
    void onOpenResourceProfileEditor();
    void onFindAssetReferences();
    void onFindProjectScripts();
    void onSetAssetsPath();
    void onFileAssociations();
    void onOpenAboutDialog();
    void onSubWindowActivated(QMdiSubWindow* window);
    void onUndoCurrentDocument();
    void onRedoCurrentDocument();
    void onCloseCurrentWindow();
    void onOpenRecentFile();
    void onClearRecentFiles();
    void onDarkTheme();
    void onLightTheme();
    void onThemeChanged(ThemeManager::Theme theme);
    void onLanguageSelected();
    bool openFileByType(const QString& fileName);

private:
    struct PreparedAssetsPathSwitch
    {
        AssetsPathSwitchParticipant* participant = nullptr;
        AssetsPathSwitchParticipant::Decision decision =
            AssetsPathSwitchParticipant::Decision::Cancelled;
        QString originalPath;
        QString targetPath;
    };

    struct PreparedEditorClose
    {
        CloseTransactionParticipant* participant = nullptr;
        ClosePlan plan;
    };

    void updateRecentFileActions();
    void addRecentFile(const QString& fileName);
    QMdiSubWindow* createMdiSubWindow(
        QWidget* widget,
        const QString& title,
        bool authoringSurface = true);
    void setupProjectExplorer();
    void setupDesktopRun();
    void compactDesktopRunDock();
    void refreshDesktopRunProjectContext();
    void refreshPlaytestTargetPresentation();
    void updateDesktopRunActionStates();
    ProjectDocumentRegistry desktopRunDocumentRegistry() const;
    QList<DesktopRunDocumentSnapshot>
        desktopRunDocumentSnapshots() const;
    void chooseDesktopGameExecutable();
    void playtestActiveTarget();
    bool runSavedScene(const QString& sceneId);
    bool runCurrentMap(MapEditorWindow* window);
    bool runCurrentScript(ScriptEditorWindow* window);
    bool runCurrentMagic(MagicEditorWindow* window);
    bool runCurrentGoodsShop(GoodsShopEditorWindow* window);
    bool runCurrentDialogue(DialogueEditorWindow* window);
    void restorePlaytestEditingPosition();
    void showStoryGraph(ScriptEditorWindow* window);
    bool refreshStoryGraphAnalysis(bool showErrors);
    bool openStoryGraphSourceLocation(
        const StoryGraphNode& node);
    bool desktopRunPrerequisitesReady(
        const QString& failureTitle);
    bool startPreparedDesktopRunTarget(
        SavedSceneLaunchPreparationResult preparation,
        const QString& failureTitle,
        const QString& preparingStatus);
    bool currentTargetVirtualPath(
        const QString& absolutePath,
        const QString& activeContentRoot,
        QString& virtualPath,
        QString& diagnosticCode) const;
    bool currentPlayableTargetVirtualPath(
        const QString& absolutePath,
        const QString& activeContentRoot,
        QString& virtualPath,
        QString& diagnosticCode) const;
    void clearDesktopRunSessionSelection();
    void synchronizeStoryGraphRuntimeTraceSession();
    bool openDesktopRunSourceLocation(
        const QString& sessionId,
        const QString& virtualPath,
        quint32 line,
        quint32 column);
    QString desktopExecutableErrorText(
        int errorValue) const;
    void showPlaytestProblem(
        const QString& title,
        const QString& message,
        const QString& details = QString());
    void refreshProjectWorkspace();
    void refreshProjectDocumentList();
    void configureScriptDocumentWindow(
        ScriptEditorWindow* window, QMdiSubWindow* subWindow);
    void configureMapDocumentWindow(
        MapEditorWindow* window, QMdiSubWindow* subWindow);
    void configureMagicDocumentWindow(
        MagicEditorWindow* window, QMdiSubWindow* subWindow);
    void configureGoodsShopDocumentWindow(
        GoodsShopEditorWindow* window, QMdiSubWindow* subWindow);
    void configureDialogueDocumentWindow(
        DialogueEditorWindow* window, QMdiSubWindow* subWindow);
    void configureNpcDataDocumentWindow(
        NpcDataEditorWindow* window, QMdiSubWindow* subWindow);
    void configureMenuDocumentWindow(
        MenuEditorWindow* window, QMdiSubWindow* subWindow);
    void configureImageDocumentWindow(
        ImageEditorWindow* window, QMdiSubWindow* subWindow);
    bool canDocumentWindowAdoptPath(
        QWidget* owner,
        const QString& currentFilePath,
        const QString& targetFilePath);
    void synchronizeScriptDocumentWindow(
        ScriptEditorWindow* window,
        QMdiSubWindow* subWindow,
        const QString& filePath,
        bool dirty);
    void synchronizeDocumentWindow(
        QMdiSubWindow* subWindow,
        const QList<ProjectDocumentState>& documents);
    void unregisterDocumentWindow(QMdiSubWindow* subWindow);
    bool activateRegisteredDocument(const QString& filePath);
    bool openDialogueReference(
        const QString& scriptPath, const QString& section,
        int line, int column);
    bool openDialogueFile(
        const QString& talkPath, const QString& section,
        const QString& callerScriptPath = QString(),
        int callerLine = 0, int callerColumn = 0);
    void openDialogueFromScript(ScriptEditorWindow* window);
    void openDialogueFromNpcScript(
        const QString& scriptPath, const QString& npcName);
    bool returnToDialogueCaller(
        const QString& scriptPath, int line, int column);
    ProjectDocumentSessionState captureProjectDocumentSession() const;
    void updateProjectDocumentSession();
    int restoreProjectDocumentSession();
    bool applyProjectSettings(bool applyAssetsPath = true);
    void captureProjectWindowPlacement();
    void applyProjectWindowPlacement();
    QString activeAssetsPath() const;
    QString resourceCollectionRoot() const;
    bool prepareAssetsPathChange(
        const QString& activeContentRoot,
        const QString& resourceCollectionRoot,
        QList<PreparedAssetsPathSwitch>& prepared);
    bool persistAssetsPathSetting(const QString& path);
    void commitAssetsPathChange(
        const QList<PreparedAssetsPathSwitch>& prepared);
    bool resolveProjectResourceContext(
        const QString& resourceCollectionRoot,
        const QString& requestedResourcePackId,
        const QString& requestedResourcePackEntryKey,
        bool allowUserSelection,
        ResourcePackSelection& selection);
    bool validateProjectResourceConfiguration(
        const QString& projectFilePath,
        const ProjectResourceConfiguration& resourceConfiguration,
        ProjectResourceConfiguration& resolvedConfiguration,
        ResourcePackSelection& selection);
    bool createProjectInternal(
        const QString& filePath,
        const ProjectResourceConfiguration& resourceConfiguration,
        const ResourcePackSelection* selection);
    bool configureProjectAssetMigration(BatchConvertWindow* window);
    bool beginProjectAssetMigration(
        const ProjectAssetMigrationContext& context,
        QString& errorMessage);
    bool finishProjectAssetMigration(
        const ProjectAssetMigrationContext& context,
        bool published,
        QString& errorMessage);
    void setProjectAssetMigrationInProgress(bool inProgress);
    void updateResourceProfileEditors();
    bool prepareEditorCloseTransaction(QList<PreparedEditorClose>& prepared);
    void commitEditorCloseTransaction(
        const QList<PreparedEditorClose>& prepared);
    void updateWindowTitle();
    QAction* activeDocumentEditAction(bool undo) const;
    QUndoStack* activeDocumentUndoStack() const;
    QWidget* activeDocumentFocusTarget() const;
    bool canExecuteActiveDocumentEditCommand(bool undo) const;
    void executeActiveDocumentEditCommand(bool undo);
    void updateEditActionStates();
    void connectDocumentEditCommandState(QWidget* widget);
    void setupLanguageMenu();
    void retranslateDynamicUi();
    bool recoverFileTransactions(const QString& assetsPath);

    Ui::MainWindow* ui;

    QLabel* statusLabel;

    static constexpr int maxRecentFiles = 8;
    QStringList recentFiles;
    QList<QAction*> recentFileActions;
    QMenu* languageMenu = nullptr;
    QActionGroup* languageActionGroup = nullptr;
    QHash<QString, QAction*> languageActions;
    QDockWidget* projectExplorerDock = nullptr;
    ProjectExplorerWidget* projectExplorerWidget = nullptr;
    QDockWidget* desktopRunDock = nullptr;
    DesktopRunPanel* desktopRunPanel = nullptr;
    DesktopRunSessionCoordinator* desktopRunCoordinator =
        nullptr;
    QAction* desktopRunAction = nullptr;
    QAction* desktopRunStopAction = nullptr;
    QPointer<QWidget> lastDocumentFocusWidget;
    QPointer<QMdiSubWindow> playtestReturnSubWindow;
    QPointer<QWidget> playtestReturnFocusWidget;
    ProjectDocumentRegistry documentRegistry;
    DesktopFileAssociationManager desktopFileAssociationManager;
    QHash<QString, QPointer<QMdiSubWindow>> documentWindows;
    QHash<QMdiSubWindow*, QSet<QString>> documentKeysByWindow;
    QPointer<QMdiSubWindow> storyGraphSubWindow;
    QPointer<StoryGraphWindow> storyGraphWindow;
    QPointer<ScriptEditorWindow> storyGraphSourceWindow;
    quint64 nextStoryGraphGeneration = 0;
    bool restoringProjectDocumentSession = false;
    bool closingApplication = false;
    bool projectAssetMigrationInProgress = false;
    QList<SavedSceneResolvedReference>
        currentDesktopRunReferences;
    QList<SavedSceneResolvedReference>
        pendingDesktopRunReferences;
    QString currentDesktopRunReferenceSessionId;
    QString currentDesktopRunSessionId;
    // True from an accepted Preparing transition until sessionPrepared or an
    // explicit preparation failure. This keeps both session consumers unbound
    // until the current run has published its workspace.
    bool desktopRunSessionBindingPending = false;
    int lastProjectDocumentRestoreFailureCount = 0;
};
