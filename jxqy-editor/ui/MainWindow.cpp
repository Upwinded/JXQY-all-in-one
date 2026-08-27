#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "BatchConvertWindow.h"
#include "AndroidExternalResourcePackageDialog.h"
#include "ImageEditorWindow.h"
#include "MapEditorWindow.h"
#include "MagicEditorWindow.h"
#include "GoodsShopEditorWindow.h"
#include "DialogueEditorWindow.h"
#include "MenuEditorWindow.h"
#include "ScriptEditorWindow.h"
#include "StoryGraphWindow.h"
#include "NpcDataEditorWindow.h"
#include "ResourceProfileEditorWindow.h"
#include "AssetReferenceDialog.h"
#include "ScriptProjectSearchDialog.h"
#include "ProjectExplorerWidget.h"
#include "ProjectSettingsDialog.h"
#include "ProjectRuntimeConfigurationDialog.h"
#include "DesktopRunPanel.h"
#include "FileAssociationDialog.h"
#include "AboutDialog.h"
#include "DarkMdiSubWindow.h"
#include "ThemeManager.h"
#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"
#include "../core/Util.h"
#include "../core/ScriptConverter.h"
#include "../core/MapFileEditor.h"
#include "../core/ProjectManager.h"
#include "../core/WindowPlacement.h"
#include "../core/EditorSettings.h"
#include "../core/TranslationManager.h"
#include "../core/DurableFileTransaction.h"
#include "../core/EditorAssetPath.h"
#include "../core/AssetReferenceScanner.h"
#include "../core/DesktopRunCurrentTarget.h"
#include "../core/DesktopRunSessionBase.h"
#include "../core/DesktopRunSessionCoordinator.h"
#include "../core/FocusedContentPlaytest.h"
#include "../core/INIFileEditor.h"
#include "../core/StoryGraphResourceContext.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QToolBar>
#include <QMdiSubWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QSettings>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QCloseEvent>
#include <QMessageBox>
#include <QProgressDialog>
#include <QActionGroup>
#include <QBrush>
#include <QEvent>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMenu>
#include <QScreen>
#include <QDockWidget>
#include <QTimer>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTextDocument>
#include <QUndoStack>

#include <limits>
#include <algorithm>
#include <optional>
#include <string>

static QSettings createSettings()
{
    return EditorSettings::create();
}

struct MapFileProbe
{
    bool readable = false;
    bool supported = false;
    bool binarySignature = false;
    bool editableText = false;
    QString errorString;
};

static bool isLikelyEditableTextSample(
    QByteArray bytes,
    bool sampleMayEndMidCharacter)
{
    if (bytes.contains('\0'))
        return false;

    for (const char byte : bytes)
    {
        const unsigned char value = static_cast<unsigned char>(byte);
        if ((value < 0x20 && value != '\t' && value != '\n' &&
             value != '\r' && value != '\f') ||
            value == 0x7f)
        {
            return false;
        }
    }

    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    if (bytes.isEmpty())
        return true;

    const int maximumTrailingBytesToIgnore =
        sampleMayEndMidCharacter ? 3 : 0;
    for (int trailingBytes = 0;
         trailingBytes <= maximumTrailingBytesToIgnore;
         ++trailingBytes)
    {
        const qsizetype candidateSize =
            bytes.size() - trailingBytes;
        if (candidateSize <= 0)
            break;
        const auto* data = reinterpret_cast<const uint8_t*>(
            bytes.constData());
        if (Util::isUtf8(
                data, static_cast<size_t>(candidateSize)))
        {
            return true;
        }
    }

    const int maximumGbkTrailingBytesToIgnore =
        sampleMayEndMidCharacter ? 1 : 0;
    for (int trailingBytes = 0;
         trailingBytes <= maximumGbkTrailingBytesToIgnore;
         ++trailingBytes)
    {
        const qsizetype candidateSize =
            bytes.size() - trailingBytes;
        if (candidateSize <= 0)
            break;
        const std::string source(
            bytes.constData(),
            static_cast<size_t>(candidateSize));
        const std::string converted =
            Util::gbkToUtf8(source);
        if (!converted.empty() &&
            Util::isUtf8(
                reinterpret_cast<const uint8_t*>(
                    converted.data()),
                converted.size()))
        {
            return true;
        }
    }
    return false;
}

static MapFileProbe probeMapFile(
    const QString& filePath)
{
    constexpr qint64 MaximumEditableTextMapSize =
        32 * 1024 * 1024;

    MapFileProbe probe;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        probe.errorString = file.errorString();
        return probe;
    }

    const QByteArray header =
        file.read(MAP_EDITOR_HEADSTR_LEN);
    if (file.error() != QFileDevice::NoError)
    {
        probe.errorString = file.errorString();
        return probe;
    }

    probe.readable = true;
    probe.supported = MapFileEditor::isMapData(
        reinterpret_cast<const uint8_t*>(
            header.constData()),
        static_cast<size_t>(header.size()));
    probe.binarySignature =
        header.startsWith("MAP File Ver");
    if (probe.supported || probe.binarySignature)
        return probe;
    if (file.size() > MaximumEditableTextMapSize)
        return probe;

    if (!file.seek(0))
    {
        probe.readable = false;
        probe.errorString = file.errorString();
        return probe;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
    {
        probe.readable = false;
        probe.errorString = file.errorString();
        return probe;
    }
    probe.editableText =
        isLikelyEditableTextSample(
            bytes,
            false);
    return probe;
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupProjectExplorer();

    setAcceptDrops(true);

    statusLabel = new QLabel(tr("就绪 - 拖放文件到此处可直接打开"), this);
    statusLabel->setObjectName(QStringLiteral("mainStatusLabel"));
    ui->statusBar->addWidget(statusLabel, 1);
    setupDesktopRun();

    for (int i = 0; i < maxRecentFiles; ++i)
    {
        auto action = new QAction(this);
        action->setVisible(false);
        connect(action, &QAction::triggered, this, &MainWindow::onOpenRecentFile);
        recentFileActions.append(action);
        ui->recentFilesMenu->insertAction(ui->clearRecentAction, action);
    }

    connect(ui->batchConvertAction, &QAction::triggered, this, &MainWindow::onBatchConvert);
    connect(
        ui->exportAndroidExternalResourceAction,
        &QAction::triggered,
        this,
        &MainWindow::onExportAndroidExternalResource);
    connect(ui->newProjectAction, &QAction::triggered, this, &MainWindow::onNewProject);
    connect(ui->openProjectAction, &QAction::triggered, this, &MainWindow::onOpenProject);
    connect(ui->saveProjectAction, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(ui->projectSettingsAction, &QAction::triggered,
        this, &MainWindow::onProjectSettings);
    connect(ui->runtimeConfigurationAction, &QAction::triggered,
        this, &MainWindow::onProjectRuntimeConfiguration);
    connect(ui->openImageEditorAction, &QAction::triggered, this, &MainWindow::onOpenImageEditor);
    connect(ui->openMapEditorAction, &QAction::triggered, this, &MainWindow::onOpenMapEditor);
    connect(ui->openMenuEditorAction, &QAction::triggered, this, &MainWindow::onOpenMenuEditor);
    connect(ui->openScriptEditorAction, &QAction::triggered, this, &MainWindow::onOpenScriptEditor);
    connect(ui->openNpcDataEditorAction, &QAction::triggered, this, &MainWindow::onOpenNpcDataEditor);
    connect(ui->openResourceProfileEditorAction, &QAction::triggered, this, &MainWindow::onOpenResourceProfileEditor);
    connect(ui->assetReferenceSearchAction, &QAction::triggered,
        this, &MainWindow::onFindAssetReferences);
    connect(ui->scriptProjectSearchAction, &QAction::triggered,
        this, &MainWindow::onFindProjectScripts);
    connect(ui->setAssetsPathAction, &QAction::triggered, this, &MainWindow::onSetAssetsPath);
    connect(ui->fileAssociationAction, &QAction::triggered,
        this, &MainWindow::onFileAssociations);
#ifndef Q_OS_WIN
    ui->fileAssociationAction->setVisible(false);
#endif
    connect(ui->exitAction, &QAction::triggered, this, &QMainWindow::close);
    connect(ui->undoAction, &QAction::triggered,
        this, &MainWindow::onUndoCurrentDocument);
    connect(ui->redoAction, &QAction::triggered,
        this, &MainWindow::onRedoCurrentDocument);
    connect(ui->closeCurrentAction, &QAction::triggered, this, &MainWindow::onCloseCurrentWindow);
    connect(ui->aboutAction, &QAction::triggered, this, &MainWindow::onOpenAboutDialog);
    connect(ui->clearRecentAction, &QAction::triggered, this, &MainWindow::onClearRecentFiles);
    connect(ui->mdiArea, &QMdiArea::subWindowActivated, this, &MainWindow::onSubWindowActivated);
    connect(qApp, &QApplication::focusChanged,
        this, [this](QWidget*, QWidget* current)
        {
            QMdiSubWindow* activeSubWindow =
                ui->mdiArea->activeSubWindow();
            QWidget* documentWidget =
                activeSubWindow
                ? activeSubWindow->widget()
                : nullptr;
            if (documentWidget && current &&
                (current == documentWidget ||
                 documentWidget->isAncestorOf(current)))
            {
                lastDocumentFocusWidget = current;
            }
            // focusChanged can be emitted while Qt is destroying a modal
            // dialog's focused child. Defer document-tree inspection until
            // widget teardown has completed to avoid reentrant access.
            QMetaObject::invokeMethod(
                this,
                &MainWindow::updateEditActionStates,
                Qt::QueuedConnection);
        });

    connect(ui->darkThemeAction, &QAction::triggered, this, &MainWindow::onDarkTheme);
    connect(ui->lightThemeAction, &QAction::triggered, this, &MainWindow::onLightTheme);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::onThemeChanged);

    setupLanguageMenu();

    onThemeChanged(ThemeManager::instance().currentTheme());

    QSettings settings = createSettings();
    recentFiles = settings.value("recentFiles").toStringList();
    updateRecentFileActions();

    bool restoredProject = false;
    const QString lastProject = settings.value("lastProject").toString();
    if (!lastProject.isEmpty() && QFileInfo::exists(lastProject))
    {
        if (ProjectManager::instance().openProject(lastProject) && applyProjectSettings())
        {
            restoredProject = true;
        }
        else
        {
            ProjectManager::instance().closeProject();
            settings.remove("lastProject");
            settings.sync();
        }
    }
    else if (!lastProject.isEmpty())
    {
        settings.remove("lastProject");
        settings.sync();
    }

    if (!restoredProject)
    {
        const QByteArray savedGeometry = settings.value("windowGeometry").toByteArray();
        const QByteArray savedState = settings.value("windowState").toByteArray();
        if (!savedGeometry.isEmpty())
            restoreGeometry(savedGeometry);
        if (!savedState.isEmpty())
            restoreState(savedState);
    }

    recoverFileTransactions(activeAssetsPath());
    refreshProjectWorkspace();
    refreshDesktopRunProjectContext();
    QTimer::singleShot(
        0,
        this,
        &MainWindow::compactDesktopRunDock);
}

MainWindow::~MainWindow()
{
    for (QMdiSubWindow* subWindow : ui->mdiArea->subWindowList())
    {
        if (auto* batchWindow =
                qobject_cast<BatchConvertWindow*>(subWindow->widget()))
        {
            batchWindow->clearProjectMigrationCallbacks();
        }
    }
    delete ui;
}

void MainWindow::prepareForApplicationExit()
{
    if (storyGraphWindow)
        storyGraphWindow->cancelAnalysis();
    if (desktopRunCoordinator)
        desktopRunCoordinator->prepareForApplicationExit();
}

void MainWindow::setupLanguageMenu()
{
    languageMenu = new QMenu(this);
    languageMenu->setObjectName(QStringLiteral("languageMenu"));
    languageActionGroup = new QActionGroup(this);
    languageActionGroup->setExclusive(true);

    for (const TranslationManager::Language& language : TranslationManager::supportedLanguages())
    {
        QAction* action = languageMenu->addAction(language.nativeName);
        action->setObjectName(QStringLiteral("language_%1_Action").arg(language.localeName));
        action->setCheckable(true);
        action->setData(language.localeName);
        languageActionGroup->addAction(action);
        languageActions.insert(language.localeName, action);
        connect(action, &QAction::triggered, this, &MainWindow::onLanguageSelected);
    }

    ui->menuBar->insertMenu(ui->themeMenu->menuAction(), languageMenu);
    retranslateDynamicUi();
}

void MainWindow::retranslateDynamicUi()
{
    if (languageMenu)
        languageMenu->setTitle(tr("语言(&L)"));

    const QString localeName = TranslationManager::instance().currentLocaleName();
    if (languageActions.contains(localeName))
        languageActions.value(localeName)->setChecked(true);

    QMdiSubWindow* currentSubWindow =
        ui->mdiArea->activeSubWindow();
    if (!currentSubWindow)
        currentSubWindow = ui->mdiArea->currentSubWindow();
    if (currentSubWindow)
        statusLabel->setText(currentSubWindow->windowTitle());
    else
        statusLabel->setText(tr("就绪 - 拖放文件到此处可直接打开"));
    QTimer::singleShot(
        0,
        this,
        [this]()
        {
            QMdiSubWindow* currentSubWindow =
                ui->mdiArea->activeSubWindow();
            if (!currentSubWindow)
                currentSubWindow =
                    ui->mdiArea->currentSubWindow();
            if (currentSubWindow)
            {
                statusLabel->setText(
                    currentSubWindow->windowTitle());
            }
            else
            {
                statusLabel->setText(
                    tr("就绪 - 拖放文件到此处可直接打开"));
            }
        });
    if (projectExplorerDock)
        projectExplorerDock->setWindowTitle(tr("项目"));
    if (desktopRunDock)
        desktopRunDock->setWindowTitle(tr("试玩"));
    if (desktopRunAction)
    {
        desktopRunAction->setText(tr("试玩"));
        desktopRunAction->setStatusTip(
            tr("试玩当前地图、当前脚本或项目默认场景"));
    }
    if (desktopRunStopAction)
    {
        desktopRunStopAction->setText(tr("停止试玩"));
        desktopRunStopAction->setStatusTip(
            tr("停止当前试玩"));
    }
    for (QMdiSubWindow* subWindow :
         ui->mdiArea->subWindowList())
    {
        if (QAction* action =
                subWindow->findChild<QAction*>(
                    QStringLiteral(
                        "desktopRunCurrentMapAction")))
        {
            action->setText(tr("试玩当前地图"));
            action->setToolTip(
                tr("使用当前地图和已打开的 NPC、物体列表试玩"));
        }
        if (QPushButton* button =
                subWindow->findChild<QPushButton*>(
                    QStringLiteral(
                        "desktopRunCurrentScriptButton")))
        {
            button->setText(tr("试玩当前脚本"));
            button->setToolTip(
                tr("使用当前脚本内容试玩"));
        }
        if (QToolButton* button =
                subWindow->findChild<QToolButton*>(
                    QStringLiteral(
                        "scriptAdvancedDebugButton")))
        {
            button->setText(tr("高级调试"));
            button->setToolTip(
                tr("分析当前脚本的控制流、剧情语义和跨文件关系"));
        }
        if (QAction* action =
                subWindow->findChild<QAction*>(
                    QStringLiteral(
                        "storyGraphCurrentScriptAction")))
        {
            action->setText(tr("剧情图"));
            action->setToolTip(
                tr("分析当前脚本的控制流、剧情语义和跨文件关系"));
        }
        if (QPushButton* button =
                subWindow->findChild<QPushButton*>(
                    QStringLiteral("editCurrentDialogueButton")))
        {
            button->setText(tr("编辑当前对话"));
            button->setToolTip(
                tr("把光标放在 talk(\"段落\") 调用所在行后打开对话"));
        }
    }
    refreshPlaytestTargetPresentation();
    updateWindowTitle();
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        retranslateDynamicUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::onLanguageSelected()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action)
        return;

    const QString requestedLocale = action->data().toString();
    TranslationManager& manager = TranslationManager::instance();
    const QString previousLocale = manager.currentLocaleName();
    if (requestedLocale == previousLocale)
        return;

    if (!manager.setLanguage(requestedLocale))
    {
        retranslateDynamicUi();
        QMessageBox::warning(this, tr("语言切换失败"), manager.lastError());
        return;
    }

    retranslateDynamicUi();
}

QMdiSubWindow* MainWindow::createMdiSubWindow(
    QWidget* widget,
    const QString& title,
    bool authoringSurface)
{
    auto subWindow = new DarkMdiSubWindow(ui->mdiArea);
    subWindow->setProperty(
        "editorAuthoringSurface",
        authoringSurface);
    const bool authoringSurfaceBlocked =
        authoringSurface &&
        projectAssetMigrationInProgress;
    subWindow->setEnabled(
        !authoringSurfaceBlocked);
    subWindow->setWidget(widget);
    subWindow->setWindowTitle(widget->windowTitle().isEmpty()
        ? title : widget->windowTitle());
    connect(widget, &QWidget::windowTitleChanged,
        subWindow, &QMdiSubWindow::setWindowTitle);
    connect(subWindow, &QWidget::windowTitleChanged, this,
        [this, subWindow](const QString& translatedTitle)
        {
            if (ui->mdiArea->activeSubWindow() == subWindow ||
                ui->mdiArea->currentSubWindow() == subWindow)
                statusLabel->setText(translatedTitle);
        });
    subWindow->setAttribute(Qt::WA_DeleteOnClose);
    subWindow->setMinimumSize(widget->minimumSize());
    ui->mdiArea->addSubWindow(subWindow);
    connectDocumentEditCommandState(widget);
    updateEditActionStates();
    return subWindow;
}

void MainWindow::setupProjectExplorer()
{
    projectExplorerDock = new QDockWidget(this);
    projectExplorerDock->setObjectName(QStringLiteral("projectExplorerDock"));
    projectExplorerDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    projectExplorerWidget = new ProjectExplorerWidget(projectExplorerDock);
    projectExplorerDock->setWidget(projectExplorerWidget);
    addDockWidget(Qt::LeftDockWidgetArea, projectExplorerDock);

    QAction* toggleAction = projectExplorerDock->toggleViewAction();
    toggleAction->setObjectName(QStringLiteral("projectExplorerToggleAction"));
    ui->windowMenu->addAction(toggleAction);

    connect(projectExplorerWidget, &ProjectExplorerWidget::fileOpenRequested,
        this, [this](const QString& filePath)
        {
            openFileByType(filePath);
        });
    connect(projectExplorerWidget,
        &ProjectExplorerWidget::documentActivationRequested,
        this, [this](const QString& filePath)
        {
            activateRegisteredDocument(filePath);
        });
}

void MainWindow::setupDesktopRun()
{
    desktopRunCoordinator =
        new DesktopRunSessionCoordinator(this);
    desktopRunPanel = new DesktopRunPanel(this);

    desktopRunDock = new QDockWidget(this);
    desktopRunDock->setObjectName(
        QStringLiteral("desktopRunDock"));
    desktopRunDock->setAllowedAreas(
        Qt::BottomDockWidgetArea |
        Qt::RightDockWidgetArea);
    desktopRunDock->setWidget(desktopRunPanel);
    addDockWidget(
        Qt::BottomDockWidgetArea,
        desktopRunDock);
    connect(
        desktopRunPanel,
        &DesktopRunPanel::detailsExpandedChanged,
        this,
        [this](bool expanded)
        {
            if (!expanded)
            {
                QTimer::singleShot(
                    0,
                    this,
                    &MainWindow::
                        compactDesktopRunDock);
            }
        });

    QAction* toggleAction =
        desktopRunDock->toggleViewAction();
    toggleAction->setObjectName(
        QStringLiteral("desktopRunToggleAction"));
    ui->windowMenu->addAction(toggleAction);

    desktopRunAction = new QAction(this);
    desktopRunAction->setObjectName(
        QStringLiteral("desktopRunAction"));
    desktopRunAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+F5")));
    desktopRunStopAction = new QAction(this);
    desktopRunStopAction->setObjectName(
        QStringLiteral("desktopRunStopAction"));
    desktopRunStopAction->setShortcut(
        QKeySequence(QStringLiteral("Shift+F5")));

    ui->mainToolBar->addSeparator();
    ui->mainToolBar->addAction(desktopRunAction);
    ui->mainToolBar->addAction(desktopRunStopAction);

    connect(
        desktopRunAction,
        &QAction::triggered,
        this,
        &MainWindow::playtestActiveTarget);
    connect(
        desktopRunStopAction,
        &QAction::triggered,
        desktopRunCoordinator,
        &DesktopRunSessionCoordinator::requestStop);
    connect(
        desktopRunPanel,
        &DesktopRunPanel::playtestRequested,
        this,
        &MainWindow::playtestActiveTarget);
    connect(
        desktopRunPanel,
        &DesktopRunPanel::stopRequested,
        desktopRunCoordinator,
        &DesktopRunSessionCoordinator::requestStop);
    connect(
        desktopRunPanel,
        &DesktopRunPanel::
            desktopExecutableSelectionRequested,
        this,
        &MainWindow::chooseDesktopGameExecutable);
    connect(
        desktopRunPanel,
        &DesktopRunPanel::sourceLocationRequested,
        this,
        [this](
            const QString& sessionId,
            const QString& file,
            quint32 line,
            quint32 column)
        {
            openDesktopRunSourceLocation(
                sessionId, file, line, column);
        });

    connect(
        desktopRunCoordinator,
        &DesktopRunSessionCoordinator::stateChanged,
        this,
        [this](DesktopRunSessionCoordinatorState state)
        {
            if (state ==
                DesktopRunSessionCoordinatorState::
                    Preparing)
            {
                desktopRunSessionBindingPending =
                    true;
                clearDesktopRunSessionSelection();
            }
            desktopRunPanel->setCoordinatorState(state);
            updateDesktopRunActionStates();
        });
    connect(
        desktopRunCoordinator,
        &DesktopRunSessionCoordinator::sessionPrepared,
        this,
        [this](
            const DesktopRunSessionPresentation& session)
        {
            desktopRunSessionBindingPending = false;
            desktopRunPanel->setSession(
                session,
                true);
            currentDesktopRunSessionId =
                session.sessionId;
            currentDesktopRunReferences =
                std::move(
                    pendingDesktopRunReferences);
            pendingDesktopRunReferences.clear();
            currentDesktopRunReferenceSessionId =
                session.sessionId;
            synchronizeStoryGraphRuntimeTraceSession();
        });
    connect(
        desktopRunCoordinator,
        &DesktopRunSessionCoordinator::
            standardOutputReceived,
        desktopRunPanel,
        &DesktopRunPanel::appendStandardOutput);
    connect(
        desktopRunCoordinator,
        &DesktopRunSessionCoordinator::
            standardErrorReceived,
        desktopRunPanel,
        &DesktopRunPanel::appendStandardError);
    connect(
        desktopRunCoordinator,
        &DesktopRunSessionCoordinator::
            terminalResultChanged,
        this,
        [this](
            const DesktopRunSessionTerminalResult& result)
        {
            desktopRunSessionBindingPending = false;
            pendingDesktopRunReferences.clear();
            desktopRunPanel->setTerminalResult(result);
            if (storyGraphWindow)
            {
                storyGraphWindow->
                    finalizeRuntimeTraceSession(
                        result.sessionId,
                        result.forcedKill);
            }
            if (result.succeeded())
            {
                statusLabel->setText(
                    tr("试玩完成，可以继续编辑。"));
            }
            else if (!result.sessionPath.isEmpty())
            {
                statusLabel->setText(
                    tr("试玩结束；可在详细信息中查看问题。"));
            }
            else
            {
                statusLabel->setText(
                    tr("试玩未能启动：%1")
                        .arg(result.message));
            }
            restorePlaytestEditingPosition();
            updateDesktopRunActionStates();
        });
}

void MainWindow::compactDesktopRunDock()
{
    if (!desktopRunDock ||
        !desktopRunPanel ||
        desktopRunPanel->detailsExpanded() ||
        dockWidgetArea(desktopRunDock) !=
            Qt::BottomDockWidgetArea)
    {
        return;
    }

    const int requestedHeight =
        (std::max)(
            desktopRunPanel->
                minimumSizeHint().height(),
            120);
    resizeDocks(
        {desktopRunDock},
        {requestedHeight},
        Qt::Vertical);
}

void MainWindow::refreshDesktopRunProjectContext()
{
    const ProjectManager& projectManager =
        ProjectManager::instance();
    desktopRunPanel->setProjectRuntimeConfiguration(
        projectManager.isProjectOpen()
        ? projectManager.runtimeConfiguration()
        : ProjectRuntimeConfiguration(),
        projectManager.isProjectOpen());
    desktopRunPanel->setActiveResourcePackId(
        projectManager.isProjectOpen()
        ? projectManager.activeResourcePackId()
        : QString());

    QSettings settings = createSettings();
    const EditorSettings::DesktopExecutableValidation
        executable =
            EditorSettings::readDesktopGameExecutable(
                settings);
    desktopRunPanel->setDesktopExecutable(
        executable.executablePath,
        executable.succeeded());
    refreshPlaytestTargetPresentation();
    updateDesktopRunActionStates();
}

void MainWindow::refreshPlaytestTargetPresentation()
{
    if (!desktopRunPanel)
        return;

    const ProjectManager& projectManager =
        ProjectManager::instance();
    if (!projectManager.isProjectOpen())
    {
        desktopRunPanel->setPlaytestTargetText(
            tr("请先打开项目"));
        return;
    }

    QMdiSubWindow* subWindow =
        ui->mdiArea->activeSubWindow();
    if (!subWindow)
        subWindow = ui->mdiArea->currentSubWindow();
    QWidget* document = subWindow
        ? subWindow->widget()
        : nullptr;

    if (auto* mapWindow =
            qobject_cast<MapEditorWindow*>(document))
    {
        QString fileName;
        for (const ProjectDocumentState& state :
             mapWindow->currentProjectDocuments())
        {
            if (state.type == ProjectDocumentType::Map)
            {
                fileName = QFileInfo(
                    state.filePath).fileName();
                break;
            }
        }
        desktopRunPanel->setPlaytestTargetText(
            fileName.isEmpty()
            ? tr("当前地图：尚未加载")
            : tr("当前地图：%1").arg(fileName));
        return;
    }

    if (auto* scriptWindow =
            qobject_cast<ScriptEditorWindow*>(document);
        scriptWindow && scriptWindow->isRunnableScript())
    {
        const QString fileName = QFileInfo(
            scriptWindow->currentFilePath()).fileName();
        desktopRunPanel->setPlaytestTargetText(
            fileName.isEmpty()
            ? tr("当前脚本：未保存脚本")
            : tr("当前脚本：%1").arg(fileName));
        return;
    }

    if (auto* magicWindow =
            qobject_cast<MagicEditorWindow*>(document))
    {
        desktopRunPanel->setPlaytestTargetText(
            tr("当前武功：%1（默认场景）")
                .arg(magicWindow->displayName()));
        return;
    }

    if (auto* goodsShopWindow =
            qobject_cast<GoodsShopEditorWindow*>(document))
    {
        desktopRunPanel->setPlaytestTargetText(
            goodsShopWindow->documentKind() == GoodsShopDocumentKind::Goods
                ? tr("当前物品：%1（默认场景）")
                      .arg(goodsShopWindow->displayName())
                : tr("当前商店：%1（默认场景）")
                      .arg(goodsShopWindow->displayName()));
        return;
    }
    if (auto* dialogueWindow =
            qobject_cast<DialogueEditorWindow*>(document))
    {
        desktopRunPanel->setPlaytestTargetText(
            tr("当前对话：%1（默认场景）")
                .arg(dialogueWindow->displayName()));
        return;
    }

    const ProjectRuntimeConfiguration& configuration =
        projectManager.runtimeConfiguration();
    for (const ProjectScene& scene : configuration.scenes)
    {
        if (scene.id != configuration.defaultSceneId)
            continue;
        const QString name = scene.name.isEmpty()
            ? tr("未命名场景")
            : scene.name;
        desktopRunPanel->setPlaytestTargetText(
            tr("默认场景：%1").arg(name));
        return;
    }

    desktopRunPanel->setPlaytestTargetText(
        tr("请选择地图、脚本，或设置默认场景"));
}

void MainWindow::updateDesktopRunActionStates()
{
    if (!desktopRunCoordinator ||
        !desktopRunPanel)
    {
        return;
    }
    const DesktopRunSessionCoordinatorState state =
        desktopRunCoordinator->state();
    const bool active =
        desktopRunCoordinator->isActive();
    const ProjectManager& projectManager =
        ProjectManager::instance();
    const bool authoringEnabled =
        !projectAssetMigrationInProgress;
    const bool runRequestEnabled = !active;
    desktopRunAction->setEnabled(
        runRequestEnabled &&
        authoringEnabled &&
        projectManager.isProjectOpen());
    desktopRunStopAction->setEnabled(
        state ==
            DesktopRunSessionCoordinatorState::Preparing ||
        state ==
            DesktopRunSessionCoordinatorState::Starting ||
        state ==
            DesktopRunSessionCoordinatorState::Running);

    ui->saveProjectAction->setEnabled(
        authoringEnabled &&
        projectManager.isProjectOpen());
    ui->newProjectAction->setEnabled(
        authoringEnabled);
    ui->openProjectAction->setEnabled(
        authoringEnabled);
    ui->projectSettingsAction->setEnabled(
        authoringEnabled &&
        projectManager.isProjectOpen());
    ui->runtimeConfigurationAction->setEnabled(
        authoringEnabled &&
        projectManager.isProjectOpen());
    ui->batchConvertAction->setEnabled(
        authoringEnabled);
    ui->exportAndroidExternalResourceAction->setEnabled(
        authoringEnabled);
    ui->openResourceProfileEditorAction->setEnabled(
        authoringEnabled);
    ui->setAssetsPathAction->setEnabled(
        authoringEnabled);
    ui->openImageEditorAction->setEnabled(
        authoringEnabled);
    ui->openMapEditorAction->setEnabled(
        authoringEnabled);
    ui->openMenuEditorAction->setEnabled(
        authoringEnabled);
    ui->openScriptEditorAction->setEnabled(
        authoringEnabled);
    ui->openNpcDataEditorAction->setEnabled(
        authoringEnabled);
    ui->scriptProjectSearchAction->setEnabled(
        authoringEnabled &&
        projectManager.isProjectOpen());
    ui->closeCurrentAction->setEnabled(
        authoringEnabled &&
        ui->mdiArea->activeSubWindow());
    const bool currentTargetRunEnabled =
        runRequestEnabled &&
        authoringEnabled &&
        projectManager.isProjectOpen();
    for (QMdiSubWindow* subWindow :
         ui->mdiArea->subWindowList())
    {
        if (QAction* action =
                subWindow->findChild<QAction*>(
                    QStringLiteral(
                        "desktopRunCurrentMapAction")))
        {
            action->setEnabled(
                currentTargetRunEnabled);
        }
        if (QPushButton* button =
                subWindow->findChild<QPushButton*>(
                    QStringLiteral(
                        "desktopRunCurrentScriptButton")))
        {
            button->setEnabled(
                currentTargetRunEnabled);
        }
    }
    QMdiSubWindow* activeSubWindow =
        ui->mdiArea->activeSubWindow();
    const bool activeAuthoringSurface =
        activeSubWindow &&
        activeSubWindow->property(
            "editorAuthoringSurface").toBool();
    for (QMdiSubWindow* subWindow :
         ui->mdiArea->subWindowList())
    {
        const bool authoringSurface =
            subWindow->property(
                "editorAuthoringSurface").toBool();
        subWindow->setEnabled(
            authoringEnabled || !authoringSurface);
    }
    if (!authoringEnabled &&
        activeAuthoringSurface &&
        storyGraphSubWindow &&
        storyGraphSubWindow->isEnabled())
    {
        storyGraphSubWindow->show();
        ui->mdiArea->setActiveSubWindow(
            storyGraphSubWindow);
        storyGraphSubWindow->raise();
    }
    if (projectExplorerDock)
    {
        projectExplorerDock->setEnabled(
            authoringEnabled);
    }
    updateRecentFileActions();
}

ProjectDocumentRegistry
MainWindow::desktopRunDocumentRegistry() const
{
    ProjectDocumentRegistry runRegistry =
        documentRegistry;
    auto includeDocuments =
        [&runRegistry](
            const QList<ProjectDocumentState>& documents)
        {
            for (ProjectDocumentState document :
                 documents)
            {
                if (document.filePath.trimmed().isEmpty())
                    continue;

                document.filePath =
                    EditorAssetPath::
                        normalizedAbsolutePath(
                            document.filePath);
                const ProjectDocumentState* existing =
                    runRegistry.findDocument(
                        document.filePath);
                if (!existing)
                {
                    runRegistry.registerDocument(
                        document.filePath,
                        document.type,
                        document.dirty);
                    continue;
                }

                runRegistry.updateDocumentState(
                    document.filePath,
                    document.type,
                    existing->dirty || document.dirty);
            }
        };

    for (QMdiSubWindow* subWindow :
         ui->mdiArea->subWindowList())
    {
        QWidget* widget = subWindow->widget();
        if (auto* scriptWindow =
                qobject_cast<ScriptEditorWindow*>(widget))
        {
            includeDocuments(
                {{scriptWindow->currentFilePath(),
                  scriptWindow->projectDocumentType(),
                  scriptWindow->hasUnsavedChanges()}});
        }
        else if (auto* mapWindow =
                     qobject_cast<MapEditorWindow*>(widget))
        {
            includeDocuments(
                mapWindow->currentProjectDocuments());
        }
        else if (auto* npcDataWindow =
                     qobject_cast<NpcDataEditorWindow*>(
                         widget))
        {
            includeDocuments(
                npcDataWindow->
                    currentProjectDocuments());
        }
        else if (auto* menuWindow =
                     qobject_cast<MenuEditorWindow*>(widget))
        {
            includeDocuments(
                menuWindow->currentProjectDocuments());
        }
        else if (auto* imageWindow =
                     qobject_cast<ImageEditorWindow*>(widget))
        {
            includeDocuments(
                imageWindow->currentProjectDocuments());
        }
        else if (auto* magicWindow =
                     qobject_cast<MagicEditorWindow*>(widget))
        {
            includeDocuments(
                magicWindow->currentProjectDocuments());
        }
        else if (auto* goodsShopWindow =
                     qobject_cast<GoodsShopEditorWindow*>(widget))
        {
            includeDocuments(
                goodsShopWindow->currentProjectDocuments());
        }
        else if (auto* dialogueWindow =
                     qobject_cast<DialogueEditorWindow*>(widget))
        {
            includeDocuments(
                dialogueWindow->currentProjectDocuments());
        }
    }
    return runRegistry;
}

QList<DesktopRunDocumentSnapshot>
MainWindow::desktopRunDocumentSnapshots() const
{
    QList<DesktopRunDocumentSnapshot> snapshots;
    for (QMdiSubWindow* subWindow :
         ui->mdiArea->subWindowList())
    {
        QWidget* widget = subWindow->widget();
        if (auto* scriptWindow =
                qobject_cast<ScriptEditorWindow*>(widget))
        {
            snapshots.append(
                scriptWindow->
                    desktopRunDocumentSnapshot());
        }
        else if (auto* mapWindow =
                     qobject_cast<MapEditorWindow*>(widget))
        {
            snapshots.append(
                mapWindow->
                    desktopRunGenericDocumentSnapshots());
        }
        else if (auto* npcDataWindow =
                     qobject_cast<NpcDataEditorWindow*>(
                         widget))
        {
            snapshots.append(
                npcDataWindow->
                    desktopRunDocumentSnapshots());
        }
        else if (auto* magicWindow =
                     qobject_cast<MagicEditorWindow*>(widget))
        {
            snapshots.append(
                magicWindow->desktopRunDocumentSnapshot());
        }
        else if (auto* goodsShopWindow =
                     qobject_cast<GoodsShopEditorWindow*>(widget))
        {
            snapshots.append(
                goodsShopWindow->desktopRunDocumentSnapshot());
        }
        else if (auto* dialogueWindow =
                     qobject_cast<DialogueEditorWindow*>(widget))
        {
            snapshots.append(
                dialogueWindow->desktopRunDocumentSnapshot());
        }
    }
    return snapshots;
}

void MainWindow::chooseDesktopGameExecutable()
{
    if (desktopRunCoordinator->isActive())
        return;

    QSettings settings = createSettings();
    const QString currentPath =
        settings.value(
            QString::fromLatin1(
                EditorSettings::DesktopGameExecutableKey))
            .toString();
    const QString selectedPath =
        QFileDialog::getOpenFileName(
            this,
            tr("选择桌面游戏可执行文件"),
            currentPath.isEmpty()
            ? QCoreApplication::applicationDirPath()
            : QFileInfo(currentPath).absolutePath(),
#ifdef Q_OS_WIN
            tr("可执行文件 (*.exe);;所有文件 (*.*)")
#else
            tr("所有文件 (*)")
#endif
        );
    if (selectedPath.isEmpty())
        return;

    const EditorSettings::DesktopExecutableValidation
        validation =
            EditorSettings::
                validateDesktopGameExecutable(
                    selectedPath);
    if (!validation.succeeded())
    {
        QMessageBox::warning(
            this,
            tr("游戏可执行文件无效"),
            desktopExecutableErrorText(
                static_cast<int>(
                    validation.error)));
        return;
    }

    QString errorMessage;
    if (!EditorSettings::writeDesktopGameExecutable(
            settings,
            selectedPath,
            &errorMessage))
    {
        QMessageBox::warning(
            this,
            tr("保存游戏可执行文件失败"),
            tr("无法把本机游戏路径写入编辑器设置：%1")
                .arg(errorMessage));
        return;
    }
    refreshDesktopRunProjectContext();
    statusLabel->setText(
        tr("桌面游戏可执行文件已更新。"));
}

QString MainWindow::desktopExecutableErrorText(
    int errorValue) const
{
    const auto error =
        static_cast<
            EditorSettings::DesktopExecutableError>(
                errorValue);
    switch (error)
    {
    case EditorSettings::DesktopExecutableError::None:
        return QString();
    case EditorSettings::DesktopExecutableError::NotSet:
        return tr("尚未选择桌面游戏可执行文件。");
    case EditorSettings::DesktopExecutableError::
        DoesNotExist:
        return tr("所选可执行文件不存在。");
    case EditorSettings::DesktopExecutableError::IsDirectory:
        return tr("所选路径是目录，不是可执行文件。");
    case EditorSettings::DesktopExecutableError::
        NotRegularFile:
        return tr("所选路径不是普通文件。");
    case EditorSettings::DesktopExecutableError::
        NotExecutable:
        return tr("所选文件没有当前平台要求的可执行权限。");
    case EditorSettings::DesktopExecutableError::
        BundleExecutableMissing:
        return tr("所选 macOS 应用包缺少有效的主可执行文件。");
    case EditorSettings::DesktopExecutableError::
        SettingsWriteFailed:
        return tr("无法保存桌面游戏可执行文件设置。");
    }
    return tr("桌面游戏可执行文件无效。");
}

bool MainWindow::desktopRunPrerequisitesReady(
    const QString& failureTitle)
{
    if (projectAssetMigrationInProgress ||
        desktopRunCoordinator->isActive())
    {
        return false;
    }
    const ProjectManager& projectManager =
        ProjectManager::instance();
    if (!projectManager.isProjectOpen())
    {
        QMessageBox::information(
            this,
            failureTitle,
            tr("请先打开一个带已保存运行配置的项目。"));
        return false;
    }

    QSettings settings = createSettings();
    const EditorSettings::DesktopExecutableValidation
        executable =
            EditorSettings::readDesktopGameExecutable(
                settings);
    if (!executable.succeeded())
    {
        QMessageBox::warning(
            this,
            failureTitle,
            desktopExecutableErrorText(
                static_cast<int>(
                    executable.error)));
        return false;
    }
    return true;
}

void MainWindow::showPlaytestProblem(
    const QString& title,
    const QString& message,
    const QString& details)
{
    QMessageBox dialog(
        QMessageBox::Warning,
        title,
        message,
        QMessageBox::Ok,
        this);
    if (!details.trimmed().isEmpty())
        dialog.setDetailedText(details);
    dialog.exec();
}

bool MainWindow::currentTargetVirtualPath(
    const QString& absolutePath,
    const QString& activeContentRoot,
    QString& virtualPath,
    QString& diagnosticCode) const
{
    virtualPath.clear();
    diagnosticCode.clear();
    if (absolutePath.trimmed().isEmpty())
    {
        diagnosticCode =
            QStringLiteral(
                "editor_run.current_target.path_required");
        return false;
    }

    const QString normalizedPath =
        EditorAssetPath::normalizedAbsolutePath(
            absolutePath);
    if (activeContentRoot.trimmed().isEmpty() ||
        !EditorAssetPath::isLexicallyInside(
            activeContentRoot,
            normalizedPath))
    {
        diagnosticCode =
            QStringLiteral(
                "editor_run.current_target.outside_active_root");
        return false;
    }

    const QFileInfo information(normalizedPath);
    if (!information.exists() ||
        !information.isFile())
    {
        diagnosticCode =
            QStringLiteral(
                "editor_run.current_target.formal_reference_missing");
        return false;
    }

    QString relativePath =
        QDir(activeContentRoot).relativeFilePath(
            normalizedPath);
    relativePath.replace(
        QLatin1Char('\\'), QLatin1Char('/'));
    if (!EditorAssetPath::normalizeResourcePath(
            relativePath,
            virtualPath))
    {
        diagnosticCode =
            QStringLiteral(
                "editor_run.current_target.outside_active_root");
        return false;
    }
    return true;
}

bool MainWindow::currentPlayableTargetVirtualPath(
    const QString& absolutePath,
    const QString& activeContentRoot,
    QString& virtualPath,
    QString& diagnosticCode) const
{
    virtualPath.clear();
    diagnosticCode.clear();
    if (absolutePath.trimmed().isEmpty())
    {
        diagnosticCode = QStringLiteral(
            "editor_run.current_target.path_required");
        return false;
    }

    const QString normalizedPath =
        EditorAssetPath::normalizedAbsolutePath(
            absolutePath);
    const QFileInfo information(normalizedPath);
    if (!information.exists() ||
        !information.isFile())
    {
        diagnosticCode = QStringLiteral(
            "editor_run.current_target.formal_reference_missing");
        return false;
    }

    const ResourceContentRootResolution resolution =
        ResourcePackScanner::resolveContentRoots(
            activeContentRoot);
    for (const ResourceContentRoot& root : resolution.roots)
    {
        if (!root.available ||
            !EditorAssetPath::isLexicallyInside(
                root.rootPath,
                normalizedPath))
        {
            continue;
        }

        QString relativePath =
            QDir(root.rootPath).relativeFilePath(
                normalizedPath);
        relativePath.replace(
            QLatin1Char('\\'), QLatin1Char('/'));
        if (EditorAssetPath::normalizeResourcePath(
                relativePath,
                virtualPath))
        {
            return true;
        }
    }

    diagnosticCode = QStringLiteral(
        "editor_run.current_target.outside_project_content");
    return false;
}

bool MainWindow::startPreparedDesktopRunTarget(
    SavedSceneLaunchPreparationResult preparation,
    const QString& failureTitle,
    const QString& preparingStatus)
{
    if (!preparation.dirtyDocuments.isEmpty())
    {
        QStringList files;
        for (const ProjectDocumentState& document :
             preparation.dirtyDocuments)
        {
            const QString fileName =
                QFileInfo(document.filePath).fileName();
            files.append(
                fileName.isEmpty()
                ? tr("未保存文档")
                : fileName);
        }
        showPlaytestProblem(
            failureTitle,
            tr("有相关内容尚未准备好。请先保存或关闭这些文档后再试玩：\n%1")
                .arg(files.join(QLatin1Char('\n'))));
        return false;
    }
    if (!preparation.issues.isEmpty() ||
        !preparation.prepared)
    {
        QStringList lines;
        for (const SavedSceneLaunchPreparationIssue& issue :
             preparation.issues)
        {
            QString line =
                issue.diagnosticCode.isEmpty()
                ? tr("准备错误 %1")
                      .arg(
                          static_cast<int>(
                              issue.error))
                : issue.diagnosticCode;
            if (!issue.fieldName.isEmpty())
                line += tr("；字段：%1").arg(issue.fieldName);
            if (!issue.virtualPath.isEmpty())
                line += tr("；资源：%1").arg(issue.virtualPath);
            if (!issue.absolutePath.isEmpty())
                line += tr("；路径：%1").arg(issue.absolutePath);
            lines.append(line);
        }
        showPlaytestProblem(
            failureTitle,
            tr("当前内容暂时不能试玩。请检查项目运行设置和相关资源；详细原因可按需展开查看。"),
            lines.join(QLatin1Char('\n')));
        return false;
    }

    QSettings settings = createSettings();
    const EditorSettings::DesktopExecutableValidation
        executable =
            EditorSettings::readDesktopGameExecutable(
                settings);
    if (!executable.succeeded())
    {
        QMessageBox::warning(
            this,
            failureTitle,
            desktopExecutableErrorText(
                static_cast<int>(
                    executable.error)));
        return false;
    }

    const DesktopRunSessionBaseResult sessionBase =
        ensureDefaultDesktopRunSessionBase();
    if (!sessionBase.succeeded())
    {
        showPlaytestProblem(
            tr("无法准备试玩"),
            tr("编辑器无法准备试玩所需的临时空间。"),
            tr("%1\n路径：%2")
                .arg(sessionBase.message,
                     sessionBase.problemPath));
        return false;
    }

    pendingDesktopRunReferences =
        preparation.prepared->references;
    desktopRunPanel->setActiveResourcePackId(
        preparation.prepared->
            canonicalActiveResourcePackId);

    DesktopRunSessionRequest request;
    request.executablePath =
        executable.executablePath;
    request.trustedSessionsBaseDirectory =
        sessionBase.path;
    request.preparedLaunch =
        std::move(*preparation.prepared);

    playtestReturnSubWindow =
        ui->mdiArea->activeSubWindow();
    if (!playtestReturnSubWindow)
    {
        playtestReturnSubWindow =
            ui->mdiArea->currentSubWindow();
    }
    playtestReturnFocusWidget.clear();
    if (playtestReturnSubWindow &&
        lastDocumentFocusWidget)
    {
        QWidget* document =
            playtestReturnSubWindow->widget();
        if (lastDocumentFocusWidget == document ||
            document->isAncestorOf(
                lastDocumentFocusWidget))
        {
            playtestReturnFocusWidget =
                lastDocumentFocusWidget;
        }
    }

    desktopRunDock->show();
    desktopRunDock->raise();
    const bool removingPreviousSession =
        desktopRunCoordinator->hasCurrentSession();
    if (removingPreviousSession)
    {
        desktopRunPanel->setSessionCleanupInProgress(true);
    }
    const bool runAccepted =
        desktopRunCoordinator->start(request);
    if (removingPreviousSession)
    {
        desktopRunPanel->setSessionCleanupInProgress(false);
    }
    if (!runAccepted)
    {
        playtestReturnSubWindow.clear();
        playtestReturnFocusWidget.clear();
        pendingDesktopRunReferences.clear();
        QMessageBox::warning(
            this,
            tr("无法启动试玩"),
            tr("试玩请求未被接受；请重新检查游戏程序和当前运行状态。"));
        return false;
    }
    currentDesktopRunReferences.clear();
    currentDesktopRunReferenceSessionId.clear();
    statusLabel->setText(preparingStatus);
    return true;
}

void MainWindow::playtestActiveTarget()
{
    desktopRunDock->show();
    desktopRunDock->raise();

    QMdiSubWindow* subWindow =
        ui->mdiArea->activeSubWindow();
    if (!subWindow)
        subWindow = ui->mdiArea->currentSubWindow();
    QWidget* document = subWindow
        ? subWindow->widget()
        : nullptr;
    if (auto* mapWindow =
            qobject_cast<MapEditorWindow*>(document))
    {
        runCurrentMap(mapWindow);
        return;
    }
    if (auto* scriptWindow =
            qobject_cast<ScriptEditorWindow*>(document);
        scriptWindow && scriptWindow->isRunnableScript())
    {
        runCurrentScript(scriptWindow);
        return;
    }
    if (auto* magicWindow =
            qobject_cast<MagicEditorWindow*>(document))
    {
        runCurrentMagic(magicWindow);
        return;
    }
    if (auto* goodsShopWindow =
            qobject_cast<GoodsShopEditorWindow*>(document))
    {
        runCurrentGoodsShop(goodsShopWindow);
        return;
    }
    if (auto* dialogueWindow =
            qobject_cast<DialogueEditorWindow*>(document))
    {
        runCurrentDialogue(dialogueWindow);
        return;
    }

    const ProjectManager& projectManager =
        ProjectManager::instance();
    if (!projectManager.isProjectOpen())
    {
        desktopRunPrerequisitesReady(
            tr("无法试玩"));
        return;
    }
    const QString defaultSceneId =
        projectManager.runtimeConfiguration().
            defaultSceneId;
    if (defaultSceneId.isEmpty())
    {
        QMessageBox::information(
            this,
            tr("没有可试玩的内容"),
            tr("请打开地图或脚本文档，或者在项目运行设置中指定默认场景。"));
        return;
    }
    runSavedScene(defaultSceneId);
}

void MainWindow::restorePlaytestEditingPosition()
{
    const QPointer<QMdiSubWindow> subWindow =
        playtestReturnSubWindow;
    const QPointer<QWidget> focusWidget =
        playtestReturnFocusWidget;
    playtestReturnSubWindow.clear();
    playtestReturnFocusWidget.clear();
    if (!subWindow)
        return;

    QTimer::singleShot(
        0,
        this,
        [this, subWindow, focusWidget]()
        {
            if (!subWindow)
                return;
            subWindow->show();
            ui->mdiArea->setActiveSubWindow(
                subWindow);
            subWindow->raise();
            if (focusWidget)
            {
                focusWidget->setFocus(
                    Qt::OtherFocusReason);
            }
            else if (subWindow->widget())
            {
                subWindow->widget()->setFocus(
                    Qt::OtherFocusReason);
            }
        });
}

bool MainWindow::runSavedScene(const QString& sceneId)
{
    const QString failureTitle =
        tr("无法试玩场景");
    if (!desktopRunPrerequisitesReady(
            failureTitle))
        return false;

    ProjectManager& projectManager =
        ProjectManager::instance();
    if (sceneId.isEmpty())
    {
        QMessageBox::information(
            this,
            failureTitle,
            tr("请在项目运行设置中指定默认场景，或打开要试玩的地图、脚本。"));
        return false;
    }

    SavedSceneLaunchPreparationResult preparation =
        prepareSavedSceneLaunch(
            projectManager,
            desktopRunDocumentRegistry(),
            sceneId,
            desktopRunDocumentSnapshots());
    return startPreparedDesktopRunTarget(
        std::move(preparation),
        tr("场景试玩准备失败"),
        tr("正在准备默认场景..."));
}

bool MainWindow::runCurrentMap(
    MapEditorWindow* window)
{
    const QString failureTitle =
        tr("无法试玩当前地图");
    if (!window ||
        !desktopRunPrerequisitesReady(
            failureTitle))
    {
        return false;
    }

    const MapEditorWindow::DesktopRunSnapshotBundle
        bundle =
            window->desktopRunSnapshotBundle();
    if (!bundle.mapLoaded)
    {
        QMessageBox::information(
            this,
            failureTitle,
            tr("地图窗口尚未加载 MAP。"));
        return false;
    }

    const QString activeContentRoot =
        activeAssetsPath();
    if (bundle.assetsBasePath.isEmpty() ||
        EditorAssetPath::logicalComparisonKey(
            bundle.assetsBasePath) !=
            EditorAssetPath::logicalComparisonKey(
                activeContentRoot))
    {
        QMessageBox::warning(
            this,
            failureTitle,
            tr("当前地图不属于这个项目，无法直接试玩。"));
        return false;
    }

    QString mapVirtualPath;
    QString diagnosticCode;
    const QString unsavedMapVirtualPath =
        QStringLiteral(
            "map/__jxqy_editor_current__/current.map");
    if (bundle.currentMapFilePath.trimmed().isEmpty())
    {
        mapVirtualPath = unsavedMapVirtualPath;
    }
    else if (!currentTargetVirtualPath(
                 bundle.currentMapFilePath,
                 activeContentRoot,
                 mapVirtualPath,
                 diagnosticCode))
    {
        QMessageBox::warning(
            this,
            failureTitle,
            tr("请先把当前地图保存到项目资源中，再重新试玩。"));
        return false;
    }

    auto companionVirtualPath =
        [this, &failureTitle, &activeContentRoot](
            bool open,
            const QString& absolutePath,
            const QString& label,
            const QString& unsavedVirtualPath,
            QString& virtualPath)
        {
            virtualPath.clear();
            if (!open)
                return true;
            if (absolutePath.trimmed().isEmpty())
            {
                virtualPath = unsavedVirtualPath;
                return true;
            }
            QString code;
            if (!currentTargetVirtualPath(
                    absolutePath,
                    activeContentRoot,
                    virtualPath,
                    code))
            {
                showPlaytestProblem(
                    failureTitle,
                    tr("请先把已打开的 %1 列表保存到项目资源中，再重新试玩。")
                        .arg(label),
                    tr("%1\n路径：%2")
                        .arg(code, absolutePath));
                return false;
            }
            return true;
        };

    QString npcVirtualPath;
    QString objectVirtualPath;
    if (!companionVirtualPath(
            bundle.npcListOpen,
            bundle.currentNpcFilePath,
            tr("NPC"),
            QStringLiteral(
                "ini/npc/__jxqy_editor_current__/current.npc"),
            npcVirtualPath) ||
        !companionVirtualPath(
            bundle.objectListOpen,
            bundle.currentObjectFilePath,
            tr("OBJ"),
            QStringLiteral(
                "ini/obj/__jxqy_editor_current__/current.obj"),
            objectVirtualPath))
    {
        return false;
    }

    ProjectManager& projectManager =
        ProjectManager::instance();
    DesktopRunCurrentTargetResult target =
        selectCurrentMapTarget(
            projectManager.runtimeConfiguration(),
            mapVirtualPath,
            npcVirtualPath,
            objectVirtualPath,
            bundle.mapWidth,
            bundle.mapHeight,
            EditorAssetPath::caseSensitivity(
                activeContentRoot),
            desktopRunPanel->selectedSceneId());
    if (!target.succeeded())
    {
        QString details = target.diagnosticCode;
        if (!target.matchingSceneIds.isEmpty())
        {
            details += tr("\n匹配场景：%1")
                .arg(
                    target.matchingSceneIds.join(
                        QStringLiteral(", ")));
        }
        showPlaytestProblem(
            failureTitle,
            tr("没有找到可用于当前地图的场景环境。请在项目运行设置中补充或选择匹配场景。"),
            details);
        return false;
    }

    QList<DesktopRunDocumentSnapshot> snapshots =
        bundle.documents;
    for (DesktopRunDocumentSnapshot& snapshot :
         snapshots)
    {
        // A current-map target is bound to the captured in-memory bytes even
        // when a direct document is currently clean.
        snapshot.includeInOverlay = true;
        if (!snapshot.filePath.trimmed().isEmpty())
            continue;
        switch (snapshot.type)
        {
        case ProjectDocumentType::Map:
            snapshot.overlayVirtualPath =
                mapVirtualPath;
            break;
        case ProjectDocumentType::NpcList:
            snapshot.overlayVirtualPath =
                npcVirtualPath;
            break;
        case ProjectDocumentType::ObjectList:
            snapshot.overlayVirtualPath =
                objectVirtualPath;
            break;
        default:
            break;
        }
    }
    SavedSceneLaunchPreparationResult preparation =
        prepareTransientSceneLaunch(
            projectManager,
            desktopRunDocumentRegistry(),
            *target.target,
            EditorRun::TargetKind::Map,
            snapshots);
    QString preparingStatus =
        tr("正在准备当前地图...");
    if (target.warningCodes.contains(
            QStringLiteral(
                "editor_run.current_map.player_position_fallback")))
    {
        preparingStatus +=
            tr(" 玩家起点超出地图，试玩将从左上角开始。");
    }
    return startPreparedDesktopRunTarget(
        std::move(preparation),
        failureTitle,
        preparingStatus);
}

bool MainWindow::runCurrentScript(
    ScriptEditorWindow* window)
{
    const QString failureTitle =
        tr("无法试玩当前脚本");
    if (!window ||
        !window->isRunnableScript() ||
        !desktopRunPrerequisitesReady(
            failureTitle))
    {
        return false;
    }

    DesktopRunDocumentSnapshot currentSnapshot =
        window->desktopRunDocumentSnapshot();
    const QString activeContentRoot =
        activeAssetsPath();
    QString scriptVirtualPath;
    QString diagnosticCode;
    if (currentSnapshot.filePath.trimmed().isEmpty())
    {
        scriptVirtualPath =
            QStringLiteral(
                "script/__jxqy_editor_current__/current.lua");
        currentSnapshot.overlayVirtualPath =
            scriptVirtualPath;
    }
    else if (!currentPlayableTargetVirtualPath(
                 currentSnapshot.filePath,
                 activeContentRoot,
                 scriptVirtualPath,
                 diagnosticCode))
    {
        showPlaytestProblem(
            failureTitle,
            tr("请先把当前脚本保存到项目资源中，再重新试玩。"),
            tr("%1\n路径：%2")
                .arg(diagnosticCode,
                     currentSnapshot.filePath));
        return false;
    }

    ProjectManager& projectManager =
        ProjectManager::instance();
    DesktopRunCurrentTargetResult target =
        selectCurrentScriptTarget(
            projectManager.runtimeConfiguration(),
            scriptVirtualPath,
            EditorAssetPath::caseSensitivity(
                activeContentRoot),
            desktopRunPanel->selectedSceneId());
    if (!target.succeeded())
    {
        QString details = target.diagnosticCode;
        if (!target.matchingSceneIds.isEmpty())
        {
            details += tr("\n匹配场景：%1")
                .arg(
                    target.matchingSceneIds.join(
                        QStringLiteral(", ")));
        }
        showPlaytestProblem(
            failureTitle,
            tr("没有找到可用于当前脚本的场景环境。请在项目运行设置中补充或选择匹配场景。"),
            details);
        return false;
    }

    QList<DesktopRunDocumentSnapshot> snapshots =
        desktopRunDocumentSnapshots();
    if (currentSnapshot.filePath.trimmed().isEmpty())
    {
        currentSnapshot.includeInOverlay = true;
        snapshots.append(currentSnapshot);
    }
    else
    {
        const QString currentKey =
            ProjectDocumentRegistry::documentPathKey(
                currentSnapshot.filePath);
        for (DesktopRunDocumentSnapshot& snapshot :
             snapshots)
        {
            if (snapshot.type ==
                    ProjectDocumentType::Script &&
                ProjectDocumentRegistry::documentPathKey(
                    snapshot.filePath) ==
                    currentKey)
            {
                // Bind diagnostics and runtime reads to this exact editor
                // buffer, including a clean buffer, without touching the
                // formal file.
                snapshot.includeInOverlay = true;
            }
        }
    }

    SavedSceneLaunchPreparationResult preparation =
        prepareTransientSceneLaunch(
            projectManager,
            desktopRunDocumentRegistry(),
            *target.target,
            EditorRun::TargetKind::Script,
            snapshots);
    return startPreparedDesktopRunTarget(
        std::move(preparation),
        failureTitle,
        tr("正在准备当前脚本..."));
}

bool MainWindow::runCurrentMagic(
    MagicEditorWindow* window)
{
    const QString failureTitle = tr("无法试玩当前武功");
    if (!window || !desktopRunPrerequisitesReady(failureTitle))
        return false;

    ProjectManager& projectManager = ProjectManager::instance();
    const QString defaultSceneId =
        projectManager.runtimeConfiguration().defaultSceneId;
    if (defaultSceneId.isEmpty())
    {
        QMessageBox::information(
            this, failureTitle,
            tr("请先在项目运行设置中指定默认场景；试玩会带入当前武功内容，进入游戏后可直接检查效果。"));
        return false;
    }

    QString magicVirtualPath;
    QString diagnosticCode;
    if (!currentTargetVirtualPath(
            window->currentFilePath(), activeAssetsPath(),
            magicVirtualPath, diagnosticCode))
    {
        showPlaytestProblem(
            failureTitle,
            tr("当前武功不属于这个项目，无法直接试玩。"),
            tr("%1\n路径：%2")
                .arg(diagnosticCode, window->currentFilePath()));
        return false;
    }

    const ProjectRuntimeConfiguration& runtimeConfiguration =
        projectManager.runtimeConfiguration();
    const auto sceneIterator = std::find_if(
        runtimeConfiguration.scenes.cbegin(),
        runtimeConfiguration.scenes.cend(),
        [&defaultSceneId](const ProjectScene& scene)
        {
            return scene.id == defaultSceneId;
        });
    if (sceneIterator == runtimeConfiguration.scenes.cend())
    {
        showPlaytestProblem(
            failureTitle,
            tr("项目默认场景已经不存在。请在项目运行设置中重新选择试玩场景。"),
            QStringLiteral(
                "editor_run.focused_content.default_scene_missing"));
        return false;
    }
    FocusedContentPlaytestBootstrap bootstrap =
        buildFocusedContentPlaytestBootstrap(
            *sceneIterator,
            FocusedContentPlaytestKind::Magic,
            magicVirtualPath,
            window->selectedLevel());
    if (!bootstrap.succeeded())
    {
        const bool sceneNotConfigured =
            bootstrap.diagnosticCode == QStringLiteral(
                "editor_run.focused_content.scene_not_configured");
        showPlaytestProblem(
            failureTitle,
            sceneNotConfigured
                ? tr("默认试玩场景还是空白初始配置。请先在项目运行设置中填写合适的玩家坐标，并按需要选择 NPC、物件列表。")
                : tr("无法为当前武功准备可直接使用的试玩场景。"),
            bootstrap.diagnosticCode);
        return false;
    }

    QList<DesktopRunDocumentSnapshot> snapshots =
        desktopRunDocumentSnapshots();
    const QString currentKey =
        ProjectDocumentRegistry::documentPathKey(
            window->currentFilePath());
    for (DesktopRunDocumentSnapshot& snapshot : snapshots)
    {
        if (snapshot.type == ProjectDocumentType::Magic &&
            ProjectDocumentRegistry::documentPathKey(snapshot.filePath) ==
                currentKey)
        {
            snapshot.includeInOverlay = true;
        }
    }
    snapshots.append(bootstrap.entryScriptSnapshot);

    SavedSceneLaunchPreparationResult preparation =
        prepareTransientSceneLaunch(
            projectManager,
            desktopRunDocumentRegistry(),
            bootstrap.scene,
            EditorRun::TargetKind::Script,
            snapshots);
    return startPreparedDesktopRunTarget(
        std::move(preparation),
        failureTitle,
        tr("正在准备当前武功“%1”的默认场景试玩...")
            .arg(window->displayName()));
}

bool MainWindow::runCurrentGoodsShop(
    GoodsShopEditorWindow* window)
{
    const bool editingGoods = window &&
        window->documentKind() == GoodsShopDocumentKind::Goods;
    const QString failureTitle = editingGoods
        ? tr("无法试玩当前物品") : tr("无法试玩当前商店");
    if (!window || !desktopRunPrerequisitesReady(failureTitle))
        return false;

    ProjectManager& projectManager = ProjectManager::instance();
    const QString defaultSceneId =
        projectManager.runtimeConfiguration().defaultSceneId;
    if (defaultSceneId.isEmpty())
    {
        QMessageBox::information(
            this, failureTitle,
            tr("请先在项目运行设置中指定默认场景；试玩会带入当前物品或商店内容，实际购买、使用和装备取决于场景。"));
        return false;
    }

    QString virtualPath;
    QString diagnosticCode;
    if (!currentTargetVirtualPath(
            window->currentFilePath(), activeAssetsPath(),
            virtualPath, diagnosticCode))
    {
        showPlaytestProblem(
            failureTitle,
            tr("当前物品或商店不属于这个项目，无法直接试玩。"),
            tr("%1\n路径：%2")
                .arg(diagnosticCode, window->currentFilePath()));
        return false;
    }

    const ProjectRuntimeConfiguration& runtimeConfiguration =
        projectManager.runtimeConfiguration();
    const auto sceneIterator = std::find_if(
        runtimeConfiguration.scenes.cbegin(),
        runtimeConfiguration.scenes.cend(),
        [&defaultSceneId](const ProjectScene& scene)
        {
            return scene.id == defaultSceneId;
        });
    if (sceneIterator == runtimeConfiguration.scenes.cend())
    {
        showPlaytestProblem(
            failureTitle,
            tr("项目默认场景已经不存在。请在项目运行设置中重新选择试玩场景。"),
            QStringLiteral(
                "editor_run.focused_content.default_scene_missing"));
        return false;
    }
    FocusedContentPlaytestBootstrap bootstrap =
        buildFocusedContentPlaytestBootstrap(
            *sceneIterator,
            editingGoods
                ? FocusedContentPlaytestKind::Goods
                : FocusedContentPlaytestKind::Shop,
            virtualPath);
    if (!bootstrap.succeeded())
    {
        const bool sceneNotConfigured =
            bootstrap.diagnosticCode == QStringLiteral(
                "editor_run.focused_content.scene_not_configured");
        showPlaytestProblem(
            failureTitle,
            sceneNotConfigured
                ? tr("默认试玩场景还是空白初始配置。请先在项目运行设置中填写合适的玩家坐标，并按需要选择 NPC、物件列表。")
                : editingGoods
                    ? tr("无法为当前物品准备加入背包的试玩场景。")
                    : tr("无法为当前商店准备直接打开的试玩场景。"),
            bootstrap.diagnosticCode);
        return false;
    }

    QList<DesktopRunDocumentSnapshot> snapshots =
        desktopRunDocumentSnapshots();
    const QString currentKey =
        ProjectDocumentRegistry::documentPathKey(
            window->currentFilePath());
    const ProjectDocumentType currentType = editingGoods
        ? ProjectDocumentType::Goods : ProjectDocumentType::Shop;
    for (DesktopRunDocumentSnapshot& snapshot : snapshots)
    {
        if (snapshot.type == currentType &&
            ProjectDocumentRegistry::documentPathKey(snapshot.filePath) ==
                currentKey)
        {
            snapshot.includeInOverlay = true;
        }
    }
    snapshots.append(bootstrap.entryScriptSnapshot);

    SavedSceneLaunchPreparationResult preparation =
        prepareTransientSceneLaunch(
            projectManager,
            desktopRunDocumentRegistry(),
            bootstrap.scene,
            EditorRun::TargetKind::Script,
            snapshots);
    return startPreparedDesktopRunTarget(
        std::move(preparation),
        failureTitle,
        editingGoods
            ? tr("正在准备当前物品“%1”的默认场景试玩...")
                  .arg(window->displayName())
            : tr("正在准备当前商店“%1”的默认场景试玩...")
                  .arg(window->displayName()));
}

bool MainWindow::runCurrentDialogue(DialogueEditorWindow* window)
{
    const QString failureTitle = tr("无法试玩当前对话");
    if (!window || !desktopRunPrerequisitesReady(failureTitle))
        return false;

    ProjectManager& projectManager = ProjectManager::instance();
    const ProjectRuntimeConfiguration& runtimeConfiguration =
        projectManager.runtimeConfiguration();
    if (runtimeConfiguration.scenes.isEmpty())
    {
        QMessageBox::information(
            this, failureTitle,
            tr("请先在项目运行设置中添加当前对话所属地图的试玩场景。"));
        return false;
    }

    QString virtualPath;
    QString diagnosticCode;
    if (!currentTargetVirtualPath(
            window->currentFilePath(), activeAssetsPath(),
            virtualPath, diagnosticCode))
    {
        showPlaytestProblem(
            failureTitle,
            tr("当前对话不属于这个项目，无法直接试玩。"),
            tr("%1\n路径：%2")
                .arg(diagnosticCode, window->currentFilePath()));
        return false;
    }

    QList<FocusedContentPlaytestBootstrap> candidates;
    bool matchingSceneNotConfigured = false;
    for (const ProjectScene& scene : runtimeConfiguration.scenes)
    {
        FocusedContentPlaytestBootstrap candidate =
            buildFocusedContentPlaytestBootstrap(
                scene,
                FocusedContentPlaytestKind::Dialogue,
                virtualPath,
                1,
                window->currentSectionName());
        if (candidate.succeeded())
            candidates.append(std::move(candidate));
        else if (candidate.diagnosticCode == QStringLiteral(
                     "editor_run.focused_content.scene_not_configured"))
        {
            matchingSceneNotConfigured = true;
        }
    }
    if (candidates.isEmpty())
    {
        showPlaytestProblem(
            failureTitle,
            matchingSceneNotConfigured
                ? tr("匹配的试玩场景还是空白初始配置。请先在项目运行设置中填写合适的玩家坐标，并按需要选择 NPC、物件列表。")
                : tr("当前对话没有匹配的地图场景。请在项目运行设置中添加该地图后再试玩。"),
            matchingSceneNotConfigured
                ? QStringLiteral(
                      "editor_run.focused_content.scene_not_configured")
                : QStringLiteral(
                      "editor_run.focused_content.dialogue_scene_missing"));
        return false;
    }

    FocusedContentPlaytestBootstrap bootstrap =
        candidates.constFirst();
    const QString selectedSceneId =
        desktopRunPanel->selectedSceneId();
    const QString preferredSceneId =
        selectedSceneId.isEmpty()
        ? runtimeConfiguration.defaultSceneId
        : selectedSceneId;
    const auto preferredCandidate = std::find_if(
        candidates.cbegin(),
        candidates.cend(),
        [&preferredSceneId](
            const FocusedContentPlaytestBootstrap& candidate)
        {
            return candidate.scene.id == preferredSceneId;
        });
    if (preferredCandidate != candidates.cend())
        bootstrap = *preferredCandidate;

    QList<DesktopRunDocumentSnapshot> snapshots =
        desktopRunDocumentSnapshots();
    const QString currentKey =
        ProjectDocumentRegistry::documentPathKey(
            window->currentFilePath());
    for (DesktopRunDocumentSnapshot& snapshot : snapshots)
    {
        if (snapshot.type == ProjectDocumentType::Dialogue &&
            ProjectDocumentRegistry::documentPathKey(snapshot.filePath) ==
                currentKey)
        {
            snapshot.includeInOverlay = true;
        }
    }
    snapshots.append(bootstrap.entryScriptSnapshot);

    SavedSceneLaunchPreparationResult preparation =
        prepareTransientSceneLaunch(
            projectManager,
            desktopRunDocumentRegistry(),
            bootstrap.scene,
            EditorRun::TargetKind::Script,
            snapshots);
    return startPreparedDesktopRunTarget(
        std::move(preparation),
        failureTitle,
        tr("正在准备对话“%1”的默认场景试玩...")
            .arg(window->displayName()));
}

void MainWindow::showStoryGraph(
    ScriptEditorWindow* window)
{
    if (!window || !window->isRunnableScript())
        return;

    if (!storyGraphWindow)
    {
        auto* graphWindow =
            new StoryGraphWindow(this);
        graphWindow->setMinimumSize(1000, 700);
        QMdiSubWindow* graphSubWindow =
            createMdiSubWindow(
                graphWindow,
                tr("剧情图"),
                false);
        storyGraphWindow = graphWindow;
        storyGraphSubWindow = graphSubWindow;

        connect(
            graphWindow,
            &StoryGraphWindow::
                sourceNavigationRequested,
            this,
            [this](const StoryGraphNode& node)
            {
                openStoryGraphSourceLocation(node);
            });
        connect(
            graphWindow,
            &StoryGraphWindow::
                analysisRefreshRequested,
            this,
            [this]()
            {
                refreshStoryGraphAnalysis(false);
            },
            Qt::QueuedConnection);
        connect(
            graphWindow,
            &StoryGraphWindow::graphWindowClosed,
            this,
            [this]()
            {
                storyGraphWindow.clear();
                storyGraphSubWindow.clear();
                storyGraphSourceWindow.clear();
            });
        connect(
            graphSubWindow,
            &QObject::destroyed,
            this,
            [this]()
            {
                storyGraphWindow.clear();
                storyGraphSubWindow.clear();
                storyGraphSourceWindow.clear();
            });
        synchronizeStoryGraphRuntimeTraceSession();
    }

    storyGraphSourceWindow = window;
    refreshStoryGraphAnalysis(true);
    if (storyGraphSubWindow)
    {
        ui->mdiArea->setActiveSubWindow(
            storyGraphSubWindow);
        storyGraphSubWindow->show();
        storyGraphSubWindow->raise();
    }
}

bool MainWindow::refreshStoryGraphAnalysis(
    bool showErrors)
{
    if (!storyGraphWindow)
        return false;

    auto reportFailure =
        [this, showErrors](
            const QString& diagnosticCode,
            const QString& message)
        {
            if (storyGraphWindow)
            {
                storyGraphWindow->showAnalysisError(
                    diagnosticCode,
                    message);
            }
            if (showErrors)
            {
                QMessageBox::warning(
                    this,
                    tr("无法生成剧情图"),
                    diagnosticCode.isEmpty()
                        ? message
                        : tr("%1\n%2")
                              .arg(
                                  diagnosticCode,
                                  message));
            }
            return false;
        };

    const ProjectManager& projectManager =
        ProjectManager::instance();
    if (!projectManager.isProjectOpen())
    {
        return reportFailure(
            QStringLiteral(
                "story_graph.project_required"),
            tr("请先打开项目，再从活动内容根中的脚本生成剧情图。"));
    }
    if (!storyGraphSourceWindow)
    {
        return reportFailure(
            QStringLiteral(
                "story_graph.entry_source_closed"),
            tr("入口脚本窗口已关闭；请从另一个脚本窗口重新打开剧情图。"));
    }

    StoryGraphProjectRequest request;
    StoryGraphResourceContext resourceContext =
        storyGraphWindow->resourceContext();
    if (!resourceContext.isValid())
    {
        QString diagnosticCode;
        QString message;
        resourceContext =
            StoryGraphResourceContext::resolve(
                resourceCollectionRoot(),
                projectManager.activeResourcePackId(),
                request.budget.
                    maximumSingleFileBytes,
                &diagnosticCode,
                &message,
                projectManager.activeResourcePackEntryKey());
        if (!resourceContext.isValid())
        {
            return reportFailure(
                diagnosticCode.isEmpty()
                    ? QStringLiteral(
                          "story_graph.resource.selection_failed")
                    : diagnosticCode,
                message.isEmpty()
                    ? tr("无法按当前项目和活动资源包解析有序内容根。")
                    : message);
        }
        storyGraphWindow->setResourceContext(
            resourceContext);
    }

    const QList<StoryGraphContentRoot> roots =
        resourceContext.orderedContentRoots();
    const auto activeRoot = std::find_if(
        roots.cbegin(),
        roots.cend(),
        [](const StoryGraphContentRoot& root)
        {
            return root.kind ==
                StoryGraphContentRootKind::Active;
        });
    if (activeRoot == roots.cend())
    {
        return reportFailure(
            QStringLiteral(
                "story_graph.resource.active_root_missing"),
            tr("有序内容根中缺少活动资源包。"));
    }

    QString entryVirtualPath;
    QString diagnosticCode;
    if (!currentTargetVirtualPath(
            storyGraphSourceWindow->
                currentFilePath(),
            resourceContext.activeContentRoot(),
            entryVirtualPath,
            diagnosticCode))
    {
        return reportFailure(
            diagnosticCode,
            tr("入口脚本必须先保存到当前活动内容根中的稳定路径：%1")
                .arg(
                    storyGraphSourceWindow->
                        currentFilePath()));
    }
    const StoryGraphReadResult entryFormalRead =
        resourceContext.probeRegularFile(
            *activeRoot,
            entryVirtualPath);
    if (entryFormalRead.status !=
            StoryGraphReadStatus::Found ||
        ProjectDocumentRegistry::documentPathKey(
            entryFormalRead.
                canonicalAbsolutePath) !=
            ProjectDocumentRegistry::documentPathKey(
                storyGraphSourceWindow->
                    currentFilePath()))
    {
        return reportFailure(
            QStringLiteral(
                "story_graph.entry_source_root_read_failed"),
            entryFormalRead.message.isEmpty()
                ? tr("入口脚本无法通过当前活动资源根路径安全读取。")
                : tr("%1\n%2")
                      .arg(
                          tr("入口脚本无法通过当前活动资源根路径安全读取。"),
                          entryFormalRead.message));
    }

    request.entrySource =
        storyGraphSourceWindow->
            storyGraphSourceSnapshot(
                activeRoot->portableRootKey,
                entryVirtualPath);
    request.orderedContentRoots = roots;
    request.includeUnknownCalls = true;

    if (request.budget.maximumSingleFileBytes <
            0 ||
        request.budget.maximumTotalBytes < 0 ||
        request.budget.maximumFileCount < 1)
    {
        return reportFailure(
            QStringLiteral(
                "story_graph.project.invalid_budget"),
            QCoreApplication::translate(
                "StoryGraphProjectResolver",
                "剧情图分析上限无效"));
    }

    qsizetype admittedSnapshotBytes = 0;
    int admittedSnapshotCount = 0;
    auto admitSnapshot =
        [&request,
         &reportFailure,
         &admittedSnapshotBytes,
         &admittedSnapshotCount](
            const StoryGraphSourceSnapshot&
                snapshot)
        {
            const qsizetype byteCount =
                snapshot.utf8Bytes.size();
            if (byteCount >
                request.budget.
                    maximumSingleFileBytes)
            {
                return reportFailure(
                    QStringLiteral(
                        "story_graph.project.budget.single_file"),
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "剧情图脚本超过单文件字节上限"));
            }
            if (byteCount >
                    request.budget.
                        maximumTotalBytes ||
                admittedSnapshotBytes >
                    request.budget.
                        maximumTotalBytes -
                        byteCount)
            {
                return reportFailure(
                    QStringLiteral(
                        "story_graph.project.budget.total_bytes"),
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "剧情图脚本总字节数超过上限"));
            }
            if (admittedSnapshotCount >=
                request.budget.maximumFileCount)
            {
                return reportFailure(
                    QStringLiteral(
                        "story_graph.project.budget.file_count"),
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "剧情图分析文件数超过上限"));
            }

            admittedSnapshotBytes += byteCount;
            ++admittedSnapshotCount;
            return true;
        };
    if (!admitSnapshot(request.entrySource))
        return false;

    const QString mapPrefix =
        QStringLiteral("script/map/");
    const int finalSlash =
        entryVirtualPath.lastIndexOf(
            QLatin1Char('/'));
    if (entryVirtualPath.startsWith(mapPrefix) &&
        finalSlash > mapPrefix.size())
    {
        const QString assumedFolder =
            entryVirtualPath.mid(
                mapPrefix.size(),
                finalSlash -
                    mapPrefix.size());
        QString mapFolderRejection;
        if (StoryGraphProjectResolver::
                isStrictRelativeVirtualPath(
                    assumedFolder,
                    &mapFolderRejection))
        {
            request.entryMapContext.state =
                StoryGraphMapContextState::Assumed;
            request.entryMapContext.
                effectiveMapFolder =
                    assumedFolder;
        }
    }

    QSet<QString> snapshotKeys;
    snapshotKeys.insert(
        activeRoot->portableRootKey +
        QLatin1Char('\n') +
        entryVirtualPath);
    for (QMdiSubWindow* subWindow :
         ui->mdiArea->subWindowList())
    {
        auto* scriptWindow =
            qobject_cast<ScriptEditorWindow*>(
                subWindow->widget());
        if (!scriptWindow ||
            !scriptWindow->isRunnableScript())
            continue;

        QString virtualPath;
        QString ignoredCode;
        if (!currentTargetVirtualPath(
                scriptWindow->currentFilePath(),
                resourceContext.activeContentRoot(),
                virtualPath,
                ignoredCode))
        {
            continue;
        }
        const StoryGraphReadResult formalRead =
            resourceContext.probeRegularFile(
                *activeRoot,
                virtualPath);
        if (formalRead.status !=
                StoryGraphReadStatus::Found ||
            ProjectDocumentRegistry::documentPathKey(
                formalRead.
                    canonicalAbsolutePath) !=
                ProjectDocumentRegistry::documentPathKey(
                    scriptWindow->
                        currentFilePath()))
        {
            continue;
        }
        const QString key =
            activeRoot->portableRootKey +
            QLatin1Char('\n') +
            virtualPath;
        if (snapshotKeys.contains(key))
            continue;
        snapshotKeys.insert(key);
        const StoryGraphSourceSnapshot snapshot =
            scriptWindow->
                storyGraphSourceSnapshot(
                    activeRoot->portableRootKey,
                    virtualPath);
        if (!admitSnapshot(snapshot))
            return false;
        request.activeRootOpenSnapshots.append(
            snapshot);
    }

    ++nextStoryGraphGeneration;
    if (nextStoryGraphGeneration == 0)
        ++nextStoryGraphGeneration;
    request.analysisGeneration =
        nextStoryGraphGeneration;
    return storyGraphWindow->submitAnalysis(
        request);
}

bool MainWindow::openStoryGraphSourceLocation(
    const StoryGraphNode& node)
{
    if (!storyGraphWindow)
        return false;

    const StoryGraphSourceIdentity& source =
        node.source;
    if (source.portableRootKey.isEmpty() ||
        source.virtualPath.isEmpty())
    {
        QMessageBox::information(
            this,
            tr("无法定位剧情图节点"),
            tr("该节点没有可导航的源码位置。"));
        return false;
    }

    QString currentAbsolutePath;
    QByteArray currentDiskBytes;
    bool matchesAnalyzedContent = false;
    const bool currentDiskSourceAvailable =
        storyGraphWindow->readCurrentDiskSource(
            source,
            currentAbsolutePath,
            currentDiskBytes,
            matchesAnalyzedContent);
    bool refreshRequired =
        storyGraphWindow->isStale() ||
        !matchesAnalyzedContent;

    QPointer<QMdiSubWindow> subWindow;
    ScriptEditorWindow* scriptWindow = nullptr;
    const auto findRunnableScriptWindow =
        [this, &subWindow, &scriptWindow](
            const QString& absolutePath)
        {
            if (absolutePath.isEmpty())
                return;
            const QString key =
                ProjectDocumentRegistry::documentPathKey(
                    absolutePath);
            QPointer<QMdiSubWindow> registered =
                documentWindows.value(key);
            auto* registeredScript =
                registered
                ? qobject_cast<ScriptEditorWindow*>(
                      registered->widget())
                : nullptr;
            if (registeredScript &&
                registeredScript->isRunnableScript())
            {
                subWindow = registered;
                scriptWindow = registeredScript;
                return;
            }
            for (QMdiSubWindow* candidate :
                 ui->mdiArea->subWindowList())
            {
                auto* candidateScript =
                    qobject_cast<ScriptEditorWindow*>(
                        candidate->widget());
                if (candidateScript &&
                    candidateScript->
                        isRunnableScript() &&
                    ProjectDocumentRegistry::documentPathKey(
                        candidateScript->
                            currentFilePath()) == key)
                {
                    subWindow = candidate;
                    scriptWindow = candidateScript;
                    return;
                }
            }
            if (registered)
                subWindow = registered;
        };

    findRunnableScriptWindow(
        currentAbsolutePath);
    if (!scriptWindow &&
        ProjectDocumentRegistry::documentPathKey(
            source.canonicalAbsolutePath) !=
            ProjectDocumentRegistry::documentPathKey(
                currentAbsolutePath))
    {
        subWindow.clear();
        findRunnableScriptWindow(
            source.canonicalAbsolutePath);
    }
    if (subWindow && !scriptWindow)
    {
        const QString occupiedPath =
            currentAbsolutePath.isEmpty()
            ? source.canonicalAbsolutePath
            : currentAbsolutePath;
        QMessageBox::warning(
            this,
            tr("无法定位剧情图节点"),
            tr("目标源码已由不兼容的文档编辑器占用：%1")
                .arg(occupiedPath));
        return false;
    }

    if (scriptWindow)
    {
        const StoryGraphSourceSnapshot current =
            scriptWindow->storyGraphSourceSnapshot(
                source.portableRootKey,
                source.virtualPath);
        if ((source.fromEditorBuffer ||
             scriptWindow->hasUnsavedChanges()) &&
            (current.identity.documentRevision !=
                 source.documentRevision ||
             current.identity.contentSha256 !=
                 source.contentSha256))
        {
            refreshRequired = true;
        }

        if (!scriptWindow->hasUnsavedChanges() &&
            currentDiskSourceAvailable)
        {
            QByteArray normalizedDiskBytes;
            const DesktopRunDocumentSnapshot
                documentSnapshot =
                    scriptWindow->
                        desktopRunDocumentSnapshot();
            const bool alreadyCurrent =
                ScriptEditorWindow::
                    normalizeDesktopRunSourceBytes(
                        currentDiskBytes,
                        normalizedDiskBytes) &&
                documentSnapshot.bytes ==
                    normalizedDiskBytes &&
                ProjectDocumentRegistry::documentPathKey(
                    documentSnapshot.filePath) ==
                    ProjectDocumentRegistry::documentPathKey(
                        currentAbsolutePath);
            if (!alreadyCurrent &&
                !scriptWindow->openFile(
                    currentAbsolutePath))
            {
                return false;
            }
        }
        else if (!currentDiskSourceAvailable)
        {
            refreshRequired = true;
        }
    }
    else if (currentDiskSourceAvailable)
    {
        auto* createdWindow =
            new ScriptEditorWindow(this);
        createdWindow->setMinimumSize(1000, 700);
        if (!createdWindow->openFile(
                currentAbsolutePath))
        {
            delete createdWindow;
            return false;
        }
        QMdiSubWindow* createdSubWindow =
            createMdiSubWindow(
                createdWindow,
                QFileInfo(
                    currentAbsolutePath).
                    fileName());
        configureScriptDocumentWindow(
            createdWindow,
            createdSubWindow);
        createdSubWindow->show();
        subWindow = createdSubWindow;
        scriptWindow = createdWindow;
    }
    else
    {
        storyGraphWindow->
            markStaleAndScheduleRefresh();
        QMessageBox::information(
            this,
            tr("无法定位剧情图节点"),
            tr("当前逻辑资源路径中未找到目标源码：%1")
                .arg(source.virtualPath));
        return false;
    }

    if (!subWindow || !scriptWindow)
        return false;
    if (refreshRequired)
    {
        storyGraphWindow->
            markStaleAndScheduleRefresh();
    }
    ui->mdiArea->setActiveSubWindow(subWindow);
    subWindow->show();
    addRecentFile(
        scriptWindow->currentFilePath());
    if (node.sourceRange.isValid())
    {
        scriptWindow->goToSourceLocation(
            node.sourceRange.start.line,
            node.sourceRange.start.column);
    }
    return true;
}

void MainWindow::clearDesktopRunSessionSelection()
{
    currentDesktopRunSessionId.clear();
    if (desktopRunPanel)
    {
        desktopRunPanel->clearSessionPresentation();
    }
    if (storyGraphWindow)
        storyGraphWindow->clearRuntimeTraceSession();
}

void MainWindow::
synchronizeStoryGraphRuntimeTraceSession()
{
    if (!storyGraphWindow ||
        !desktopRunCoordinator ||
        currentDesktopRunSessionId.isEmpty() ||
        desktopRunSessionBindingPending)
    {
        if (storyGraphWindow)
            storyGraphWindow->
                clearRuntimeTraceSession();
        return;
    }

    const auto presentation =
        desktopRunCoordinator->
            currentSessionPresentation();
    if (!presentation.has_value() ||
        presentation->sessionId !=
            currentDesktopRunSessionId ||
        presentation->paths.runtimeTracePath.
            isEmpty())
    {
        storyGraphWindow->
            clearRuntimeTraceSession();
        return;
    }

    const bool producerActive =
        desktopRunCoordinator->isActive();
    const DesktopRunSessionTerminalResult
        terminalResult =
            desktopRunCoordinator->
                terminalResult();
    const bool forcedTermination =
        terminalResult.sessionId ==
                currentDesktopRunSessionId &&
            terminalResult.forcedKill;
    storyGraphWindow->bindRuntimeTraceSession(
        currentDesktopRunSessionId,
        presentation->paths.runtimeTracePath,
        producerActive,
        forcedTermination);
}

bool MainWindow::openDesktopRunSourceLocation(
    const QString& sessionId,
    const QString& virtualPath,
    quint32 line,
    quint32 column)
{
    if (sessionId.isEmpty() ||
        sessionId !=
            currentDesktopRunReferenceSessionId)
    {
        QMessageBox::warning(
            this,
            tr("无法打开诊断位置"),
            tr("诊断记录不属于当前显示的桌面运行会话。"));
        return false;
    }

    const SavedSceneResolvedReference* reference = nullptr;
    for (const SavedSceneResolvedReference& candidate :
         currentDesktopRunReferences)
    {
        if (candidate.virtualPath == virtualPath &&
            candidate.kind ==
                SavedSceneReferenceKind::EntryScript)
        {
            reference = &candidate;
            break;
        }
    }
    if (!reference)
    {
        QMessageBox::warning(
            this,
            tr("无法打开诊断位置"),
            tr("诊断文件不属于当前运行场景的已验证直接引用：%1")
                .arg(virtualPath));
        return false;
    }

    const bool hasSupportedSourceLocation =
        line > 0 &&
        line <=
            static_cast<quint32>(
                (std::numeric_limits<int>::max)()) &&
        column <=
            static_cast<quint32>(
                (std::numeric_limits<int>::max)());

    if (reference->launchSourceFromEditorBuffer)
    {
        const QString key =
            ProjectDocumentRegistry::documentPathKey(
                reference->absolutePath);
        QPointer<QMdiSubWindow> sourceSubWindow =
            documentWindows.value(key);
        auto* sourceWindow =
            sourceSubWindow
            ? qobject_cast<ScriptEditorWindow*>(
                  sourceSubWindow->widget())
            : nullptr;
        if (sourceWindow &&
            !sourceWindow->isRunnableScript())
        {
            sourceWindow = nullptr;
        }
        if (!sourceWindow)
        {
            for (QMdiSubWindow* candidate :
                 ui->mdiArea->subWindowList())
            {
                auto* candidateScript =
                    qobject_cast<ScriptEditorWindow*>(
                        candidate->widget());
                if (candidateScript &&
                    candidateScript->isRunnableScript() &&
                    ProjectDocumentRegistry::documentPathKey(
                        candidateScript->
                            currentFilePath()) == key)
                {
                    sourceSubWindow = candidate;
                    sourceWindow = candidateScript;
                    break;
                }
            }
        }
        if (!sourceWindow || !sourceSubWindow)
        {
            QMessageBox::warning(
                this,
                tr("无法打开诊断位置"),
                tr("editor_run.source.binding_missing"
                   "\n诊断绑定的是启动时的编辑缓冲区，但该缓冲区已关闭：%1")
                    .arg(reference->absolutePath));
            return false;
        }

        const DesktopRunDocumentSnapshot snapshot =
            sourceWindow->
                desktopRunDocumentSnapshot();
        if (!snapshot.serializationSupported ||
            ProjectDocumentRegistry::documentPathKey(
                snapshot.filePath) != key ||
            snapshot.bytes !=
                reference->launchVerifiedBytes)
        {
            QMessageBox::warning(
                this,
                tr("无法打开诊断位置"),
                tr("editor_run.source.buffer_changed"
                   "\n脚本缓冲区在本次运行启动后已变化；"
                   "为避免定位到错误内容，请重新运行后再打开：%1")
                    .arg(reference->absolutePath));
            return false;
        }

        ui->mdiArea->setActiveSubWindow(
            sourceSubWindow);
        sourceSubWindow->show();
        addRecentFile(reference->absolutePath);
        if (hasSupportedSourceLocation)
        {
            sourceWindow->goToSourceLocation(
                static_cast<int>(line),
                static_cast<int>(column));
        }
        return true;
    }

    const QString key =
        ProjectDocumentRegistry::documentPathKey(
            reference->absolutePath);
    QPointer<QMdiSubWindow> subWindow =
        documentWindows.value(key);
    auto* scriptWindow =
        subWindow
        ? qobject_cast<ScriptEditorWindow*>(
              subWindow->widget())
        : nullptr;
    if (scriptWindow &&
        !scriptWindow->isRunnableScript())
    {
        scriptWindow = nullptr;
    }

    // Common/dependency scripts may intentionally live outside the active
    // project-explorer root and therefore are not in documentWindows.
    if (!subWindow)
    {
        for (QMdiSubWindow* candidate :
             ui->mdiArea->subWindowList())
        {
            auto* candidateScript =
                qobject_cast<ScriptEditorWindow*>(
                    candidate->widget());
            if (candidateScript &&
                candidateScript->isRunnableScript() &&
                ProjectDocumentRegistry::documentPathKey(
                    candidateScript->currentFilePath()) ==
                    key)
            {
                subWindow = candidate;
                scriptWindow = candidateScript;
                break;
            }
        }
    }
    if (subWindow && !scriptWindow)
    {
        QMessageBox::warning(
            this,
            tr("无法打开诊断位置"),
            tr("诊断源文件已由不兼容的文档编辑器占用：%1")
                .arg(reference->absolutePath));
        return false;
    }
    if (scriptWindow)
    {
        ui->mdiArea->setActiveSubWindow(subWindow);
        subWindow->show();
    }
    else
    {
        auto* newWindow =
            new ScriptEditorWindow(this);
        newWindow->setMinimumSize(1000, 700);
        if (!newWindow->openFile(
                reference->absolutePath))
        {
            delete newWindow;
            return false;
        }
        QMdiSubWindow* createdSubWindow =
            createMdiSubWindow(
                newWindow,
                QFileInfo(reference->absolutePath).
                    fileName());
        configureScriptDocumentWindow(
            newWindow, createdSubWindow);
        createdSubWindow->show();
        subWindow = createdSubWindow;
        scriptWindow = newWindow;
    }
    addRecentFile(reference->absolutePath);

    if (scriptWindow &&
        hasSupportedSourceLocation)
    {
        scriptWindow->goToSourceLocation(
            static_cast<int>(line),
            static_cast<int>(column));
    }
    return true;
}

void MainWindow::refreshProjectWorkspace()
{
    documentRegistry.clear();
    documentWindows.clear();
    documentKeysByWindow.clear();

    const ProjectManager& projectManager = ProjectManager::instance();
    const QString contentRoot = projectManager.isProjectOpen()
        ? activeAssetsPath() : QString();
    projectExplorerWidget->setContentRoot(contentRoot);

    for (QMdiSubWindow* subWindow : ui->mdiArea->subWindowList())
    {
        auto* scriptWindow =
            qobject_cast<ScriptEditorWindow*>(subWindow->widget());
        if (scriptWindow)
        {
            synchronizeScriptDocumentWindow(
                scriptWindow,
                subWindow,
                scriptWindow->currentFilePath(),
                scriptWindow->hasUnsavedChanges());
            continue;
        }

        auto* mapWindow =
            qobject_cast<MapEditorWindow*>(subWindow->widget());
        if (mapWindow)
        {
            synchronizeDocumentWindow(
                subWindow, mapWindow->currentProjectDocuments());
            continue;
        }

        auto* menuWindow =
            qobject_cast<MenuEditorWindow*>(subWindow->widget());
        if (menuWindow)
        {
            synchronizeDocumentWindow(
                subWindow, menuWindow->currentProjectDocuments());
            continue;
        }

        auto* imageWindow =
            qobject_cast<ImageEditorWindow*>(subWindow->widget());
        if (imageWindow)
        {
            synchronizeDocumentWindow(
                subWindow, imageWindow->currentProjectDocuments());
            continue;
        }

        auto* npcDataWindow =
            qobject_cast<NpcDataEditorWindow*>(subWindow->widget());
        if (npcDataWindow)
        {
            synchronizeDocumentWindow(
                subWindow, npcDataWindow->currentProjectDocuments());
            continue;
        }

        auto* magicWindow =
            qobject_cast<MagicEditorWindow*>(subWindow->widget());
        if (magicWindow)
        {
            synchronizeDocumentWindow(
                subWindow, magicWindow->currentProjectDocuments());
            continue;
        }

        auto* goodsShopWindow =
            qobject_cast<GoodsShopEditorWindow*>(subWindow->widget());
        if (goodsShopWindow)
        {
            synchronizeDocumentWindow(
                subWindow, goodsShopWindow->currentProjectDocuments());
            continue;
        }

        auto* dialogueWindow =
            qobject_cast<DialogueEditorWindow*>(subWindow->widget());
        if (dialogueWindow)
        {
            synchronizeDocumentWindow(
                subWindow, dialogueWindow->currentProjectDocuments());
        }
    }
    refreshProjectDocumentList();
    if (desktopRunPanel)
    {
        if (desktopRunCoordinator &&
            !desktopRunCoordinator->isActive())
        {
            currentDesktopRunReferences.clear();
            pendingDesktopRunReferences.clear();
            currentDesktopRunReferenceSessionId.clear();
            clearDesktopRunSessionSelection();
        }
        refreshDesktopRunProjectContext();
    }
}

void MainWindow::refreshProjectDocumentList()
{
    projectExplorerWidget->setDocuments(documentRegistry.documents());
}

void MainWindow::configureScriptDocumentWindow(
    ScriptEditorWindow* window, QMdiSubWindow* subWindow)
{
    if (window->isRunnableScript())
    {
        if (QHBoxLayout* toolbarLayout =
            window->findChild<QHBoxLayout*>(
                QStringLiteral("toolbarLayout")))
        {
            auto* runButton =
                new QPushButton(
                    tr("试玩当前脚本"),
                    window);
            runButton->setObjectName(
                QStringLiteral(
                    "desktopRunCurrentScriptButton"));
            runButton->setToolTip(
                tr("使用当前脚本内容试玩"));
            toolbarLayout->insertWidget(3, runButton);
            connect(
                runButton,
                &QPushButton::clicked,
                this,
                [this, window]()
                {
                    runCurrentScript(window);
                });

            auto* advancedDebugButton =
                new QToolButton(window);
            advancedDebugButton->setObjectName(
                QStringLiteral(
                    "scriptAdvancedDebugButton"));
            advancedDebugButton->setText(
                tr("高级调试"));
            advancedDebugButton->setToolButtonStyle(
                Qt::ToolButtonTextOnly);
            advancedDebugButton->setPopupMode(
                QToolButton::InstantPopup);
            advancedDebugButton->setToolTip(
                tr("分析当前脚本的控制流、剧情语义和跨文件关系"));

            auto* advancedDebugMenu =
                new QMenu(advancedDebugButton);
            advancedDebugMenu->setObjectName(
                QStringLiteral(
                    "scriptAdvancedDebugMenu"));
            auto* storyGraphAction =
                advancedDebugMenu->addAction(
                    tr("剧情图"));
            storyGraphAction->setObjectName(
                QStringLiteral(
                    "storyGraphCurrentScriptAction"));
            storyGraphAction->setToolTip(
                tr("分析当前脚本的控制流、剧情语义和跨文件关系"));
            advancedDebugButton->setMenu(
                advancedDebugMenu);
            toolbarLayout->insertWidget(
                4, advancedDebugButton);
            connect(
                storyGraphAction,
                &QAction::triggered,
                this,
                [this, window]()
                {
                    showStoryGraph(window);
                });

            auto* dialogueButton = new QPushButton(
                tr("编辑当前对话"), window);
            dialogueButton->setObjectName(
                QStringLiteral("editCurrentDialogueButton"));
            dialogueButton->setToolTip(
                tr("把光标放在 talk(\"段落\") 调用所在行后打开对话"));
            toolbarLayout->insertWidget(5, dialogueButton);
            connect(dialogueButton, &QPushButton::clicked,
                    this, [this, window]()
                    {
                        openDialogueFromScript(window);
                    });
        }
    }
    window->setDocumentPathValidator(
        [this, window](const QString& filePath)
        {
            return canDocumentWindowAdoptPath(
                window, window->currentFilePath(), filePath);
        });
    connect(window, &ScriptEditorWindow::documentStateChanged,
        this, [this, window, subWindow](const QString& filePath, bool dirty)
        {
            synchronizeScriptDocumentWindow(
                window, subWindow, filePath, dirty);
            if (ui->mdiArea->activeSubWindow() ==
                subWindow)
            {
                refreshPlaytestTargetPresentation();
            }
            if (window->isRunnableScript() &&
                storyGraphWindow)
            {
                storyGraphWindow->
                    markStaleAndScheduleRefresh();
            }
        });
    connect(
        window,
        &ScriptEditorWindow::storyGraphSourceChanged,
        this,
        [this, window]()
        {
            if (window->isRunnableScript() &&
                storyGraphWindow)
            {
                storyGraphWindow->
                    markStaleAndScheduleRefresh();
            }
        });
    connect(window, &ScriptEditorWindow::documentClosed,
        this, [this, window, subWindow]()
        {
            if (window->isRunnableScript() &&
                storyGraphWindow)
            {
                if (storyGraphSourceWindow == window)
                {
                    storyGraphWindow->cancelAnalysis();
                    storyGraphSourceWindow.clear();
                    storyGraphWindow->showAnalysisError(
                        QStringLiteral(
                            "story_graph.entry_source_closed"),
                        tr("入口脚本窗口已关闭；请从另一个脚本窗口重新打开剧情图。"));
                }
                else
                {
                    storyGraphWindow->
                        markStaleAndScheduleRefresh();
                }
            }
            unregisterDocumentWindow(subWindow);
            QTimer::singleShot(0, this,
                [this]() { updateProjectDocumentSession(); });
        });
    synchronizeScriptDocumentWindow(
        window, subWindow,
        window->currentFilePath(), window->hasUnsavedChanges());
    refreshPlaytestTargetPresentation();
    updateDesktopRunActionStates();
}

void MainWindow::configureMapDocumentWindow(
    MapEditorWindow* window, QMdiSubWindow* subWindow)
{
    QAction* runAction =
        new QAction(tr("试玩当前地图"), window);
    runAction->setObjectName(
        QStringLiteral(
            "desktopRunCurrentMapAction"));
    runAction->setToolTip(
        tr("使用当前地图和已打开的 NPC、物体列表试玩"));
    if (QMenu* fileMenu =
            window->findChild<QMenu*>(
                QStringLiteral("mapFileMenu")))
    {
        fileMenu->addSeparator();
        fileMenu->addAction(runAction);
    }
    else if (QToolBar* toolBar =
                 window->findChild<QToolBar*>(
                     QStringLiteral("mapToolBar")))
    {
        toolBar->addSeparator();
        toolBar->addAction(runAction);
    }
    connect(
        runAction,
        &QAction::triggered,
        this,
        [this, window]()
        {
            runCurrentMap(window);
        });
    window->setDocumentPathValidator(
        [this, window](const QString& currentPath,
                       const QString& targetPath)
        {
            return canDocumentWindowAdoptPath(
                window, currentPath, targetPath);
        });
    connect(window, &MapEditorWindow::documentStatesChanged,
        this, [this, window, subWindow]()
        {
            synchronizeDocumentWindow(
                subWindow, window->currentProjectDocuments());
            if (ui->mdiArea->activeSubWindow() ==
                subWindow)
            {
                refreshPlaytestTargetPresentation();
            }
        });
    connect(window, &MapEditorWindow::documentClosed,
        this, [this, subWindow]()
        {
            unregisterDocumentWindow(subWindow);
            QTimer::singleShot(0, this,
                [this]() { updateProjectDocumentSession(); });
        });
    synchronizeDocumentWindow(
        subWindow, window->currentProjectDocuments());
    refreshPlaytestTargetPresentation();
    updateDesktopRunActionStates();
}

void MainWindow::configureMagicDocumentWindow(
    MagicEditorWindow* window, QMdiSubWindow* subWindow)
{
    window->setDocumentPathValidator(
        [this, window](const QString& currentPath,
                       const QString& targetPath)
        {
            return canDocumentWindowAdoptPath(
                window, currentPath, targetPath);
        });
    connect(window, &MagicEditorWindow::documentStatesChanged,
        this, [this, window, subWindow]()
        {
            synchronizeDocumentWindow(
                subWindow, window->currentProjectDocuments());
            if (ui->mdiArea->activeSubWindow() == subWindow)
                refreshPlaytestTargetPresentation();
        });
    connect(window, &MagicEditorWindow::documentClosed,
        this, [this, subWindow]()
        {
            unregisterDocumentWindow(subWindow);
            QTimer::singleShot(
                0, this, [this]() { updateProjectDocumentSession(); });
        });
    connect(window, &MagicEditorWindow::openMagicFileRequested,
        this, [this](const QString& path) { openFileByType(path); });
    connect(window, &MagicEditorWindow::playtestRequested,
        this, [this, window]() { runCurrentMagic(window); });
    synchronizeDocumentWindow(
        subWindow, window->currentProjectDocuments());
    refreshPlaytestTargetPresentation();
    updateDesktopRunActionStates();
}

void MainWindow::configureGoodsShopDocumentWindow(
    GoodsShopEditorWindow* window, QMdiSubWindow* subWindow)
{
    window->setDocumentPathValidator(
        [this, window](const QString& currentPath,
                       const QString& targetPath)
        {
            return canDocumentWindowAdoptPath(
                window, currentPath, targetPath);
        });
    connect(window, &GoodsShopEditorWindow::documentStatesChanged,
        this, [this, window, subWindow]()
        {
            synchronizeDocumentWindow(
                subWindow, window->currentProjectDocuments());
            if (ui->mdiArea->activeSubWindow() == subWindow)
                refreshPlaytestTargetPresentation();
        });
    connect(window, &GoodsShopEditorWindow::documentClosed,
        this, [this, subWindow]()
        {
            unregisterDocumentWindow(subWindow);
            QTimer::singleShot(
                0, this, [this]() { updateProjectDocumentSession(); });
        });
    connect(window, &GoodsShopEditorWindow::openGoodsShopFileRequested,
        this, [this](const QString& path) { openFileByType(path); });
    connect(window, &GoodsShopEditorWindow::playtestRequested,
        this, [this, window]() { runCurrentGoodsShop(window); });
    synchronizeDocumentWindow(
        subWindow, window->currentProjectDocuments());
    refreshPlaytestTargetPresentation();
    updateDesktopRunActionStates();
}

void MainWindow::configureDialogueDocumentWindow(
    DialogueEditorWindow* window, QMdiSubWindow* subWindow)
{
    window->setDocumentPathValidator(
        [this, window](const QString& currentPath,
                       const QString& targetPath)
        {
            return canDocumentWindowAdoptPath(
                window, currentPath, targetPath);
        });
    connect(window, &DialogueEditorWindow::documentStatesChanged,
        this, [this, window, subWindow]()
        {
            synchronizeDocumentWindow(
                subWindow, window->currentProjectDocuments());
            if (ui->mdiArea->activeSubWindow() == subWindow)
                refreshPlaytestTargetPresentation();
        });
    connect(window, &DialogueEditorWindow::documentClosed,
        this, [this, subWindow]()
        {
            unregisterDocumentWindow(subWindow);
            QTimer::singleShot(
                0, this, [this]() { updateProjectDocumentSession(); });
        });
    connect(window, &DialogueEditorWindow::playtestRequested,
        this, [this, window]() { runCurrentDialogue(window); });
    connect(window, &DialogueEditorWindow::returnToCallerRequested,
        this, [this](const QString& path, int line, int column)
        {
            returnToDialogueCaller(path, line, column);
        });
    synchronizeDocumentWindow(
        subWindow, window->currentProjectDocuments());
    refreshPlaytestTargetPresentation();
    updateDesktopRunActionStates();
}

void MainWindow::configureNpcDataDocumentWindow(
    NpcDataEditorWindow* window, QMdiSubWindow* subWindow)
{
    window->setDocumentPathValidator(
        [this, window](const QString& currentPath,
                       const QString& targetPath)
        {
            return canDocumentWindowAdoptPath(
                window, currentPath, targetPath);
        });
    connect(window, &NpcDataEditorWindow::documentStatesChanged,
        this, [this, window, subWindow]()
        {
            synchronizeDocumentWindow(
                subWindow, window->currentProjectDocuments());
        });
    connect(window, &NpcDataEditorWindow::documentClosed,
        this, [this, subWindow]()
        {
            subWindow->setProperty("jxqyDocumentClosed", true);
            unregisterDocumentWindow(subWindow);
            updateProjectDocumentSession();
        });
    connect(window, &NpcDataEditorWindow::editDialogueFromNpcRequested,
        this, [this](const QString& scriptPath, const QString& npcName)
        {
            openDialogueFromNpcScript(scriptPath, npcName);
        });
    synchronizeDocumentWindow(
        subWindow, window->currentProjectDocuments());
}

void MainWindow::configureMenuDocumentWindow(
    MenuEditorWindow* window, QMdiSubWindow* subWindow)
{
    window->setDocumentPathValidator(
        [this, window](const QString& currentPath,
                       const QString& targetPath)
        {
            return canDocumentWindowAdoptPath(
                window, currentPath, targetPath);
        });
    connect(window, &MenuEditorWindow::documentStatesChanged,
        this, [this, window, subWindow]()
        {
            synchronizeDocumentWindow(
                subWindow, window->currentProjectDocuments());
        });
    connect(window, &MenuEditorWindow::documentClosed,
        this, [this, subWindow]()
        {
            unregisterDocumentWindow(subWindow);
            QTimer::singleShot(0, this,
                [this]() { updateProjectDocumentSession(); });
        });
    synchronizeDocumentWindow(
        subWindow, window->currentProjectDocuments());
}

void MainWindow::configureImageDocumentWindow(
    ImageEditorWindow* window, QMdiSubWindow* subWindow)
{
    window->setDocumentPathValidator(
        [this, window](const QString& currentPath,
                       const QString& targetPath)
        {
            return canDocumentWindowAdoptPath(
                window, currentPath, targetPath);
        });
    connect(window, &ImageEditorWindow::documentStatesChanged,
        this, [this, window, subWindow]()
        {
            synchronizeDocumentWindow(
                subWindow, window->currentProjectDocuments());
        });
    connect(window, &ImageEditorWindow::documentClosed,
        this, [this, subWindow]()
        {
            unregisterDocumentWindow(subWindow);
            QTimer::singleShot(0, this,
                [this]() { updateProjectDocumentSession(); });
        });
    synchronizeDocumentWindow(
        subWindow, window->currentProjectDocuments());
}

bool MainWindow::canDocumentWindowAdoptPath(
    QWidget* owner,
    const QString& currentFilePath,
    const QString& targetFilePath)
{
    const ProjectManager& projectManager = ProjectManager::instance();
    const QString contentRoot = projectExplorerWidget->contentRoot();
    if (!projectManager.isProjectOpen() || contentRoot.isEmpty() ||
        !EditorAssetPath::isLexicallyInside(
            contentRoot,
            targetFilePath))
    {
        return true;
    }

    const QString key =
        ProjectDocumentRegistry::documentPathKey(targetFilePath);
    const QPointer<QMdiSubWindow> existingWindow = documentWindows.value(key);
    if (!existingWindow)
        return true;

    const bool sameOwner = existingWindow->widget() == owner;
    const bool sameDocument = !currentFilePath.isEmpty() &&
        ProjectDocumentRegistry::documentPathKey(currentFilePath) == key;
    if (sameOwner && sameDocument)
        return true;

    ui->mdiArea->setActiveSubWindow(existingWindow);
    existingWindow->show();
    statusLabel->setText(tr("文档已打开: %1").arg(targetFilePath));
    return false;
}

void MainWindow::synchronizeScriptDocumentWindow(
    ScriptEditorWindow* window,
    QMdiSubWindow* subWindow,
    const QString& filePath,
    bool dirty)
{
    QList<ProjectDocumentState> documents;
    if (!filePath.isEmpty())
    {
        documents.append(
            {filePath,
             window->projectDocumentType(),
             dirty});
    }
    synchronizeDocumentWindow(subWindow, documents);
}

void MainWindow::synchronizeDocumentWindow(
    QMdiSubWindow* subWindow,
    const QList<ProjectDocumentState>& documents)
{
    const ProjectManager& projectManager = ProjectManager::instance();
    const QString contentRoot = projectExplorerWidget->contentRoot();
    QList<ProjectDocumentState> acceptedDocuments;
    QSet<QString> acceptedKeys;

    if (projectManager.isProjectOpen() && !contentRoot.isEmpty())
    {
        for (ProjectDocumentState document : documents)
        {
            if (document.filePath.isEmpty() ||
                !EditorAssetPath::isLexicallyInside(
                    contentRoot, document.filePath))
            {
                continue;
            }

            document.filePath =
                EditorAssetPath::normalizedAbsolutePath(document.filePath);
            const QString key =
                ProjectDocumentRegistry::documentPathKey(
                    document.filePath);
            if (acceptedKeys.contains(key))
                continue;

            const QPointer<QMdiSubWindow> existingWindow =
                documentWindows.value(key);
            if (existingWindow && existingWindow != subWindow)
                continue;

            acceptedKeys.insert(key);
            acceptedDocuments.append(document);
        }
    }

    const QSet<QString> oldKeys = documentKeysByWindow.value(subWindow);
    for (const QString& oldKey : oldKeys)
    {
        if (acceptedKeys.contains(oldKey))
            continue;

        const ProjectDocumentState* oldDocument =
            documentRegistry.findDocument(oldKey);
        if (oldDocument)
        {
            const QString oldPath = oldDocument->filePath;
            documentRegistry.unregisterDocument(oldPath);
        }
        if (documentWindows.value(oldKey) == subWindow)
            documentWindows.remove(oldKey);
    }

    for (const ProjectDocumentState& document : acceptedDocuments)
    {
        const QString key =
            ProjectDocumentRegistry::documentPathKey(
                document.filePath);
        if (!documentRegistry.contains(document.filePath))
        {
            documentRegistry.registerDocument(
                document.filePath, document.type, document.dirty);
        }
        else
        {
            documentRegistry.updateDocumentState(
                document.filePath, document.type, document.dirty);
        }
        documentWindows.insert(key, subWindow);
    }

    if (acceptedKeys.isEmpty())
        documentKeysByWindow.remove(subWindow);
    else
        documentKeysByWindow.insert(subWindow, acceptedKeys);
    refreshProjectDocumentList();
    updateProjectDocumentSession();
}

void MainWindow::unregisterDocumentWindow(QMdiSubWindow* subWindow)
{
    const QSet<QString> keys = documentKeysByWindow.take(subWindow);
    if (keys.isEmpty())
        return;

    for (const QString& key : keys)
    {
        const ProjectDocumentState* document =
            documentRegistry.findDocument(key);
        if (document)
        {
            const QString filePath = document->filePath;
            documentRegistry.unregisterDocument(filePath);
        }
        if (documentWindows.value(key) == subWindow)
            documentWindows.remove(key);
    }
    refreshProjectDocumentList();
}

bool MainWindow::activateRegisteredDocument(const QString& filePath)
{
    const QString key =
        ProjectDocumentRegistry::documentPathKey(filePath);
    const QPointer<QMdiSubWindow> subWindow = documentWindows.value(key);
    if (!subWindow)
    {
        const ProjectDocumentState* document =
            documentRegistry.findDocument(filePath);
        if (document)
            documentRegistry.unregisterDocument(document->filePath);
        documentWindows.remove(key);
        refreshProjectDocumentList();
        return false;
    }

    ui->mdiArea->setActiveSubWindow(subWindow);
    subWindow->show();
    return true;
}

ProjectDocumentSessionState MainWindow::captureProjectDocumentSession() const
{
    ProjectDocumentSessionState session;
    const ProjectManager& projectManager = ProjectManager::instance();
    const QString contentRoot = projectExplorerWidget->contentRoot();
    if (!projectManager.isProjectOpen() || contentRoot.isEmpty())
        return session;

    auto relativeProjectPath = [&contentRoot](const QString& filePath)
    {
        QString relativePath;
        if (filePath.isEmpty() ||
            !EditorAssetPath::isLexicallyInside(
                contentRoot,
                filePath) ||
            !EditorAssetPath::normalizeResourcePath(
                QDir(contentRoot).relativeFilePath(filePath), relativePath))
        {
            return QString();
        }
        return relativePath;
    };

    QHash<QMdiSubWindow*, QString> primaryPaths;
    const QList<QMdiSubWindow*> subWindows =
        ui->mdiArea->subWindowList(QMdiArea::CreationOrder);
    for (QMdiSubWindow* subWindow : subWindows)
    {
        if (subWindow->property("jxqyDocumentClosed").toBool())
            continue;
        if (auto* scriptWindow =
                qobject_cast<ScriptEditorWindow*>(subWindow->widget()))
        {
            const QString primaryPath =
                relativeProjectPath(scriptWindow->currentFilePath());
            if (primaryPath.isEmpty())
                continue;

            ProjectSessionWindowState window;
            window.type = ProjectSessionWindowType::Script;
            window.primaryPath = primaryPath;
            const ScriptEditorWindow::EditingViewState viewState =
                scriptWindow->editingViewState();
            window.textView.isValid = true;
            window.textView.cursorPosition =
                viewState.cursorPosition;
            window.textView.verticalScrollValue =
                viewState.verticalScrollValue;
            window.textView.horizontalScrollValue =
                viewState.horizontalScrollValue;
            session.windows.append(window);
            primaryPaths.insert(subWindow, primaryPath);
            continue;
        }

        if (auto* npcDataWindow =
                qobject_cast<NpcDataEditorWindow*>(subWindow->widget()))
        {
            const QList<ProjectDocumentState> documents =
                npcDataWindow->currentProjectDocuments();
            if (documents.isEmpty() || documents.size() > 3)
                continue;

            ProjectSessionWindowState window;
            QString npcListPath;
            QString objectListPath;
            QString npcResourcePath;
            bool validDocumentSet = true;
            for (const ProjectDocumentState& currentDocument : documents)
            {
                const QString relativePath =
                    relativeProjectPath(currentDocument.filePath);
                if (relativePath.isEmpty())
                {
                    validDocumentSet = false;
                    break;
                }
                switch (currentDocument.type)
                {
                case ProjectDocumentType::NpcList:
                    if (!npcListPath.isEmpty())
                        validDocumentSet = false;
                    npcListPath = relativePath;
                    break;
                case ProjectDocumentType::ObjectList:
                    if (!objectListPath.isEmpty())
                        validDocumentSet = false;
                    objectListPath = relativePath;
                    break;
                case ProjectDocumentType::NpcResource:
                    if (!npcResourcePath.isEmpty() ||
                        !NpcDataEditorWindow::isNpcResourceFilePath(
                            currentDocument.filePath))
                    {
                        validDocumentSet = false;
                    }
                    npcResourcePath = relativePath;
                    break;
                case ProjectDocumentType::Script:
                case ProjectDocumentType::Text:
                case ProjectDocumentType::Map:
                case ProjectDocumentType::Menu:
                case ProjectDocumentType::Image:
                case ProjectDocumentType::Magic:
                case ProjectDocumentType::Goods:
                case ProjectDocumentType::Shop:
                case ProjectDocumentType::Dialogue:
                    validDocumentSet = false;
                    break;
                }
                if (!validDocumentSet)
                    break;
            }
            if (!validDocumentSet)
                continue;

            if (!npcListPath.isEmpty())
            {
                window.type = ProjectSessionWindowType::NpcList;
                window.primaryPath = npcListPath;
                window.objectListPath = objectListPath;
                window.npcResourcePath = npcResourcePath;
            }
            else if (!objectListPath.isEmpty())
            {
                window.type = ProjectSessionWindowType::ObjectList;
                window.primaryPath = objectListPath;
                window.npcResourcePath = npcResourcePath;
            }
            else if (!npcResourcePath.isEmpty())
            {
                window.type = ProjectSessionWindowType::NpcResource;
                window.primaryPath = npcResourcePath;
            }
            else
                continue;

            session.windows.append(window);
            primaryPaths.insert(subWindow, window.primaryPath);
            continue;
        }

        if (auto* menuWindow =
                qobject_cast<MenuEditorWindow*>(subWindow->widget()))
        {
            const QList<ProjectDocumentState> documents =
                menuWindow->currentProjectDocuments();
            if (documents.size() != 1 ||
                documents.front().type != ProjectDocumentType::Menu)
            {
                continue;
            }

            ProjectSessionWindowState window;
            window.type = ProjectSessionWindowType::Menu;
            window.primaryPath =
                relativeProjectPath(documents.front().filePath);
            if (window.primaryPath.isEmpty())
                continue;

            session.windows.append(window);
            primaryPaths.insert(subWindow, window.primaryPath);
            continue;
        }

        if (auto* imageWindow =
                qobject_cast<ImageEditorWindow*>(subWindow->widget()))
        {
            const QList<ProjectDocumentState> documents =
                imageWindow->currentProjectDocuments();
            if (documents.size() != 1 ||
                documents.front().type != ProjectDocumentType::Image)
            {
                continue;
            }

            ProjectSessionWindowState window;
            window.type = ProjectSessionWindowType::Image;
            window.primaryPath =
                relativeProjectPath(documents.front().filePath);
            if (window.primaryPath.isEmpty())
                continue;

            session.windows.append(window);
            primaryPaths.insert(subWindow, window.primaryPath);
            continue;
        }

        if (auto* magicWindow =
                qobject_cast<MagicEditorWindow*>(subWindow->widget()))
        {
            const QList<ProjectDocumentState> documents =
                magicWindow->currentProjectDocuments();
            if (documents.size() != 1 ||
                documents.front().type != ProjectDocumentType::Magic)
            {
                continue;
            }

            ProjectSessionWindowState window;
            window.type = ProjectSessionWindowType::Magic;
            window.primaryPath =
                relativeProjectPath(documents.front().filePath);
            if (window.primaryPath.isEmpty())
                continue;

            session.windows.append(window);
            primaryPaths.insert(subWindow, window.primaryPath);
            continue;
        }

        if (auto* goodsShopWindow =
                qobject_cast<GoodsShopEditorWindow*>(subWindow->widget()))
        {
            const QList<ProjectDocumentState> documents =
                goodsShopWindow->currentProjectDocuments();
            if (documents.size() != 1 ||
                (documents.front().type != ProjectDocumentType::Goods &&
                 documents.front().type != ProjectDocumentType::Shop))
            {
                continue;
            }

            ProjectSessionWindowState window;
            window.type = documents.front().type == ProjectDocumentType::Goods
                ? ProjectSessionWindowType::Goods
                : ProjectSessionWindowType::Shop;
            window.primaryPath =
                relativeProjectPath(documents.front().filePath);
            if (window.primaryPath.isEmpty())
                continue;

            session.windows.append(window);
            primaryPaths.insert(subWindow, window.primaryPath);
            continue;
        }

        if (auto* dialogueWindow =
                qobject_cast<DialogueEditorWindow*>(subWindow->widget()))
        {
            const QList<ProjectDocumentState> documents =
                dialogueWindow->currentProjectDocuments();
            if (documents.size() != 1 ||
                documents.front().type != ProjectDocumentType::Dialogue)
            {
                continue;
            }

            ProjectSessionWindowState window;
            window.type = ProjectSessionWindowType::Dialogue;
            window.primaryPath =
                relativeProjectPath(documents.front().filePath);
            if (window.primaryPath.isEmpty())
                continue;

            session.windows.append(window);
            primaryPaths.insert(subWindow, window.primaryPath);
            continue;
        }

        auto* mapWindow =
            qobject_cast<MapEditorWindow*>(subWindow->widget());
        if (!mapWindow)
            continue;

        ProjectSessionWindowState window;
        window.type = ProjectSessionWindowType::Map;
        const QList<ProjectDocumentState> documents =
            mapWindow->currentProjectDocuments();
        for (const ProjectDocumentState& document : documents)
        {
            const QString relativePath =
                relativeProjectPath(document.filePath);
            if (relativePath.isEmpty())
                continue;
            switch (document.type)
            {
            case ProjectDocumentType::Map:
                window.primaryPath = relativePath;
                break;
            case ProjectDocumentType::NpcList:
                window.npcListPath = relativePath;
                break;
            case ProjectDocumentType::ObjectList:
                window.objectListPath = relativePath;
                break;
            case ProjectDocumentType::Script:
            case ProjectDocumentType::Text:
            case ProjectDocumentType::Menu:
            case ProjectDocumentType::Image:
            case ProjectDocumentType::Magic:
            case ProjectDocumentType::Goods:
            case ProjectDocumentType::Shop:
            case ProjectDocumentType::Dialogue:
            case ProjectDocumentType::NpcResource:
                break;
            }
        }
        if (window.primaryPath.isEmpty())
            continue;

        const MapEditorWindow::EditingViewState viewState =
            mapWindow->editingViewState();
        window.mapView.isValid = true;
        window.mapView.zoomLevel = viewState.zoomLevel;
        window.mapView.scrollX = viewState.scrollX;
        window.mapView.scrollY = viewState.scrollY;
        session.windows.append(window);
        primaryPaths.insert(subWindow, window.primaryPath);
    }

    session.activeWindowPath =
        primaryPaths.value(ui->mdiArea->activeSubWindow());
    return session;
}

void MainWindow::updateProjectDocumentSession()
{
    ProjectManager& projectManager = ProjectManager::instance();
    if (restoringProjectDocumentSession || closingApplication ||
        !projectManager.isProjectOpen())
        return;
    projectManager.setDocumentSession(captureProjectDocumentSession());
}

int MainWindow::restoreProjectDocumentSession()
{
    ProjectManager& projectManager = ProjectManager::instance();
    if (!projectManager.isProjectOpen())
        return 0;

    const ProjectDocumentSessionState storedSession =
        projectManager.documentSession();
    int failureCount = projectManager.documentSessionNeedsRepair() ? 1 : 0;
    const QString contentRoot = projectExplorerWidget->contentRoot();
    restoringProjectDocumentSession = true;

    auto npcDataDocumentsMatch = [](NpcDataEditorWindow* npcDataWindow,
                                    const QList<ProjectDocumentState>& expected)
    {
        if (!npcDataWindow)
            return false;
        const QList<ProjectDocumentState> actual =
            npcDataWindow->currentProjectDocuments();
        if (actual.size() != expected.size())
            return false;
        for (const ProjectDocumentState& expectedDocument : expected)
        {
            bool found = false;
            for (const ProjectDocumentState& actualDocument : actual)
            {
                if (actualDocument.type == expectedDocument.type &&
                    ProjectDocumentRegistry::documentPathKey(
                        actualDocument.filePath) ==
                        ProjectDocumentRegistry::documentPathKey(
                            expectedDocument.filePath))
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        return true;
    };

    for (const ProjectSessionWindowState& window : storedSession.windows)
    {
        QString primaryPath;
        QPointer<QMdiSubWindow> previousSubWindow;
        if (!EditorAssetPath::resolveLogicalResourcePath(
                contentRoot, window.primaryPath, primaryPath))
        {
            ++failureCount;
            continue;
        }
        const QString resolvedPrimaryKey =
            ProjectDocumentRegistry::documentPathKey(primaryPath);

        const bool isNpcDataSession =
            window.type == ProjectSessionWindowType::NpcList ||
            window.type == ProjectSessionWindowType::ObjectList ||
            window.type == ProjectSessionWindowType::NpcResource;
        if (isNpcDataSession)
        {
            QList<ProjectDocumentState> expectedDocuments;
            const ProjectDocumentType primaryType =
                window.type == ProjectSessionWindowType::NpcList
                    ? ProjectDocumentType::NpcList
                    : window.type == ProjectSessionWindowType::ObjectList
                        ? ProjectDocumentType::ObjectList
                        : ProjectDocumentType::NpcResource;
            expectedDocuments.append({primaryPath, primaryType, false});

            auto appendCompanion =
                [&](const QString& relativePath,
                    ProjectDocumentType type)
            {
                if (relativePath.isEmpty())
                    return true;
                QString absolutePath;
                if (!EditorAssetPath::resolveLogicalResourcePath(
                        contentRoot, relativePath, absolutePath) ||
                    !QFileInfo::exists(absolutePath) ||
                    (type == ProjectDocumentType::NpcResource &&
                     !NpcDataEditorWindow::isNpcResourceFilePath(
                         absolutePath)))
                {
                    return false;
                }
                expectedDocuments.append({absolutePath, type, false});
                return true;
            };

            const bool validPrimaryType =
                QFileInfo::exists(primaryPath) &&
                (primaryType != ProjectDocumentType::NpcResource ||
                 NpcDataEditorWindow::isNpcResourceFilePath(primaryPath));
            bool validDocumentSet = validPrimaryType;
            if (window.type == ProjectSessionWindowType::NpcList)
            {
                validDocumentSet = validDocumentSet &&
                    appendCompanion(window.objectListPath,
                        ProjectDocumentType::ObjectList) &&
                    appendCompanion(window.npcResourcePath,
                        ProjectDocumentType::NpcResource);
            }
            else if (window.type == ProjectSessionWindowType::ObjectList)
            {
                validDocumentSet = validDocumentSet &&
                    appendCompanion(window.npcResourcePath,
                        ProjectDocumentType::NpcResource);
            }
            if (!validDocumentSet)
            {
                ++failureCount;
                continue;
            }

            const QPointer<QMdiSubWindow> existingPrimaryWindow =
                documentWindows.value(resolvedPrimaryKey);
            bool ownershipConflict = false;
            for (const ProjectDocumentState& document : expectedDocuments)
            {
                const QPointer<QMdiSubWindow> owner = documentWindows.value(
                    ProjectDocumentRegistry::documentPathKey(
                        document.filePath));
                if (owner && (!existingPrimaryWindow ||
                              owner != existingPrimaryWindow))
                {
                    ownershipConflict = true;
                    break;
                }
            }
            if (ownershipConflict)
            {
                ++failureCount;
                continue;
            }

            if (existingPrimaryWindow)
            {
                auto* existingNpcDataWindow =
                    qobject_cast<NpcDataEditorWindow*>(
                        existingPrimaryWindow->widget());
                if (!npcDataDocumentsMatch(
                        existingNpcDataWindow, expectedDocuments))
                {
                    ++failureCount;
                    continue;
                }
                ui->mdiArea->setActiveSubWindow(existingPrimaryWindow);
                existingPrimaryWindow->show();
                continue;
            }

            if (!openFileByType(primaryPath))
            {
                ++failureCount;
                continue;
            }
            const QPointer<QMdiSubWindow> subWindow =
                documentWindows.value(resolvedPrimaryKey);
            auto* npcDataWindow = subWindow
                ? qobject_cast<NpcDataEditorWindow*>(subWindow->widget())
                : nullptr;
            bool restored = npcDataWindow != nullptr;
            for (int index = 1;
                 restored && index < expectedDocuments.size(); ++index)
            {
                const ProjectDocumentState& document =
                    expectedDocuments[index];
                switch (document.type)
                {
                case ProjectDocumentType::NpcList:
                    restored = npcDataWindow->openNpcFile(
                        document.filePath, false);
                    break;
                case ProjectDocumentType::ObjectList:
                    restored = npcDataWindow->openObjectFile(
                        document.filePath, false);
                    break;
                case ProjectDocumentType::NpcResource:
                    restored = npcDataWindow->openNpcResourceFile(
                        document.filePath, false);
                    break;
                case ProjectDocumentType::Script:
                case ProjectDocumentType::Text:
                case ProjectDocumentType::Map:
                case ProjectDocumentType::Menu:
                case ProjectDocumentType::Image:
                case ProjectDocumentType::Magic:
                case ProjectDocumentType::Goods:
                case ProjectDocumentType::Shop:
                case ProjectDocumentType::Dialogue:
                    restored = false;
                    break;
                }
            }
            restored = restored &&
                npcDataDocumentsMatch(npcDataWindow, expectedDocuments);
            if (!restored)
            {
                if (subWindow && subWindow->close())
                    ui->mdiArea->removeSubWindow(subWindow);
                ++failureCount;
            }
            continue;
        }

        previousSubWindow = documentWindows.value(resolvedPrimaryKey);
        if (!QFileInfo::exists(primaryPath) || !openFileByType(primaryPath))
        {
            ++failureCount;
            continue;
        }

        const QPointer<QMdiSubWindow> subWindow =
            documentWindows.value(resolvedPrimaryKey);
        bool restoredTypeMatches = false;
        switch (window.type)
        {
        case ProjectSessionWindowType::Script:
            restoredTypeMatches = subWindow &&
                qobject_cast<ScriptEditorWindow*>(subWindow->widget());
            break;
        case ProjectSessionWindowType::Map:
            restoredTypeMatches = subWindow &&
                qobject_cast<MapEditorWindow*>(subWindow->widget());
            break;
        case ProjectSessionWindowType::NpcList:
        case ProjectSessionWindowType::ObjectList:
        case ProjectSessionWindowType::NpcResource:
        {
            auto* npcDataWindow = subWindow
                ? qobject_cast<NpcDataEditorWindow*>(subWindow->widget())
                : nullptr;
            const QList<ProjectDocumentState> documents = npcDataWindow
                ? npcDataWindow->currentProjectDocuments()
                : QList<ProjectDocumentState>();
            const ProjectDocumentType expectedType =
                window.type == ProjectSessionWindowType::NpcList
                    ? ProjectDocumentType::NpcList
                    : window.type == ProjectSessionWindowType::ObjectList
                        ? ProjectDocumentType::ObjectList
                        : ProjectDocumentType::NpcResource;
            restoredTypeMatches = documents.size() == 1 &&
                documents.front().type == expectedType &&
                ProjectDocumentRegistry::documentPathKey(
                    documents.front().filePath) ==
                    resolvedPrimaryKey;
            break;
        }
        case ProjectSessionWindowType::Menu:
        {
            auto* menuWindow = subWindow
                ? qobject_cast<MenuEditorWindow*>(subWindow->widget())
                : nullptr;
            const QList<ProjectDocumentState> documents = menuWindow
                ? menuWindow->currentProjectDocuments()
                : QList<ProjectDocumentState>();
            restoredTypeMatches = documents.size() == 1 &&
                documents.front().type == ProjectDocumentType::Menu &&
                ProjectDocumentRegistry::documentPathKey(
                    documents.front().filePath) ==
                    resolvedPrimaryKey;
            break;
        }
        case ProjectSessionWindowType::Image:
        {
            auto* imageWindow = subWindow
                ? qobject_cast<ImageEditorWindow*>(subWindow->widget())
                : nullptr;
            const QList<ProjectDocumentState> documents = imageWindow
                ? imageWindow->currentProjectDocuments()
                : QList<ProjectDocumentState>();
            restoredTypeMatches = documents.size() == 1 &&
                documents.front().type == ProjectDocumentType::Image &&
                ProjectDocumentRegistry::documentPathKey(
                    documents.front().filePath) ==
                    resolvedPrimaryKey;
            break;
        }
        case ProjectSessionWindowType::Magic:
        {
            auto* magicWindow = subWindow
                ? qobject_cast<MagicEditorWindow*>(subWindow->widget())
                : nullptr;
            const QList<ProjectDocumentState> documents = magicWindow
                ? magicWindow->currentProjectDocuments()
                : QList<ProjectDocumentState>();
            restoredTypeMatches = documents.size() == 1 &&
                documents.front().type == ProjectDocumentType::Magic &&
                ProjectDocumentRegistry::documentPathKey(
                    documents.front().filePath) ==
                    resolvedPrimaryKey;
            break;
        }
        case ProjectSessionWindowType::Goods:
        case ProjectSessionWindowType::Shop:
        {
            auto* goodsShopWindow = subWindow
                ? qobject_cast<GoodsShopEditorWindow*>(subWindow->widget())
                : nullptr;
            const QList<ProjectDocumentState> documents = goodsShopWindow
                ? goodsShopWindow->currentProjectDocuments()
                : QList<ProjectDocumentState>();
            const ProjectDocumentType expectedType =
                window.type == ProjectSessionWindowType::Goods
                    ? ProjectDocumentType::Goods
                    : ProjectDocumentType::Shop;
            restoredTypeMatches = documents.size() == 1 &&
                documents.front().type == expectedType &&
                ProjectDocumentRegistry::documentPathKey(
                    documents.front().filePath) ==
                    resolvedPrimaryKey;
            break;
        }
        case ProjectSessionWindowType::Dialogue:
        {
            auto* dialogueWindow = subWindow
                ? qobject_cast<DialogueEditorWindow*>(subWindow->widget())
                : nullptr;
            const QList<ProjectDocumentState> documents = dialogueWindow
                ? dialogueWindow->currentProjectDocuments()
                : QList<ProjectDocumentState>();
            restoredTypeMatches = documents.size() == 1 &&
                documents.front().type == ProjectDocumentType::Dialogue &&
                ProjectDocumentRegistry::documentPathKey(
                    documents.front().filePath) ==
                    resolvedPrimaryKey;
            break;
        }
        }
        if (!restoredTypeMatches)
        {
            if (!previousSubWindow && subWindow)
            {
                if (subWindow->close())
                    ui->mdiArea->removeSubWindow(subWindow);
            }
            ++failureCount;
            continue;
        }

        if (window.type == ProjectSessionWindowType::Script)
        {
            auto* scriptWindow = subWindow
                ? qobject_cast<ScriptEditorWindow*>(subWindow->widget())
                : nullptr;
            if (scriptWindow && window.textView.isValid)
            {
                ScriptEditorWindow::EditingViewState viewState;
                viewState.cursorPosition =
                    window.textView.cursorPosition;
                viewState.verticalScrollValue =
                    window.textView.verticalScrollValue;
                viewState.horizontalScrollValue =
                    window.textView.horizontalScrollValue;
                scriptWindow->restoreEditingViewState(viewState);
                const QPointer<ScriptEditorWindow> guardedScriptWindow =
                    scriptWindow;
                QTimer::singleShot(
                    0,
                    scriptWindow,
                    [this, guardedScriptWindow, viewState]()
                    {
                        if (!guardedScriptWindow)
                            return;
                        guardedScriptWindow->restoreEditingViewState(
                            viewState);
                        updateProjectDocumentSession();
                    });
            }
        }

        if (window.type != ProjectSessionWindowType::Map)
            continue;

        auto* mapWindow = subWindow
            ? qobject_cast<MapEditorWindow*>(subWindow->widget()) : nullptr;
        if (!mapWindow)
        {
            ++failureCount;
            continue;
        }

        auto resolveOptionalDocument =
            [&contentRoot, &failureCount](const QString& relativePath)
        {
            if (relativePath.isEmpty())
                return QString();
            QString absolutePath;
            if (!EditorAssetPath::resolveLogicalResourcePath(
                    contentRoot, relativePath, absolutePath) ||
                !QFileInfo::exists(absolutePath))
            {
                ++failureCount;
                return QString();
            }
            return absolutePath;
        };

        const QString npcListPath =
            resolveOptionalDocument(window.npcListPath);
        const QString objectListPath =
            resolveOptionalDocument(window.objectListPath);
        const MapEditorWindow::ProjectListRestoreResult result =
            mapWindow->restoreProjectListDocuments(
                npcListPath, objectListPath);
        if (!result.npcListRestored && !window.npcListPath.isEmpty())
            ++failureCount;
        if (!result.objectListRestored && !window.objectListPath.isEmpty())
            ++failureCount;
        if (window.mapView.isValid)
        {
            MapEditorWindow::EditingViewState viewState;
            viewState.zoomLevel = window.mapView.zoomLevel;
            viewState.scrollX = window.mapView.scrollX;
            viewState.scrollY = window.mapView.scrollY;
            mapWindow->restoreEditingViewState(viewState);
            const QPointer<MapEditorWindow> guardedMapWindow =
                mapWindow;
            QTimer::singleShot(
                0,
                mapWindow,
                [this, guardedMapWindow, viewState]()
                {
                    if (!guardedMapWindow)
                        return;
                    guardedMapWindow->restoreEditingViewState(
                        viewState);
                    updateProjectDocumentSession();
                });
        }
    }

    if (!storedSession.activeWindowPath.isEmpty())
    {
        QString activeWindowPath;
        if (EditorAssetPath::resolveLogicalResourcePath(
                contentRoot, storedSession.activeWindowPath,
                activeWindowPath))
        {
            activateRegisteredDocument(activeWindowPath);
        }
    }

    restoringProjectDocumentSession = false;
    projectManager.setDocumentSession(captureProjectDocumentSession());
    if (failureCount > 0)
    {
        const QString message =
            tr("项目已打开，%1 个会话文档未恢复").arg(failureCount);
        statusLabel->setText(message);
        ui->statusBar->showMessage(message, 8000);
    }
    return failureCount;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls)
    {
        QString fileName = url.toLocalFile();
        if (!fileName.isEmpty())
        {
            openFileByType(fileName);
        }
    }
}

bool MainWindow::openStartupFileArguments(
    const QStringList& fileArguments)
{
    if (fileArguments.isEmpty())
        return true;

    if (fileArguments.size() != 1)
    {
        ui->statusBar->showMessage(
            tr("启动时一次只能打开一个路径。"), 5000);
        return false;
    }

    const QString filePath = fileArguments.constFirst();
    if (filePath.isEmpty())
    {
        ui->statusBar->showMessage(
            tr("启动文件路径不能为空。"), 5000);
        return false;
    }

    return openFileByType(
        EditorAssetPath::normalizedAbsolutePath(filePath));
}

void MainWindow::openDialogueFromScript(ScriptEditorWindow* window)
{
    if (!window)
        return;
    const std::optional<DialogueReference> reference =
        window->dialogueReferenceAtCursor();
    if (!reference)
    {
        QMessageBox::information(
            this, tr("没有可编辑的对话"),
            tr("请把光标放在 talk(\"段落\") 调用所在行，再选择“编辑当前对话”。"));
        return;
    }
    openDialogueReference(
        window->currentFilePath(), reference->section,
        reference->line, reference->column);
}

void MainWindow::openDialogueFromNpcScript(
    const QString& scriptPath, const QString& npcName)
{
    QFile file(scriptPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::information(
            this, tr("无法编辑对话"),
            tr("无法读取“%1”的脚本：\n%2")
                .arg(npcName, scriptPath));
        return;
    }
    QByteArray normalizedBytes;
    if (!ScriptEditorWindow::normalizeDesktopRunSourceBytes(
            file.readAll(), normalizedBytes))
    {
        QMessageBox::information(
            this, tr("无法编辑对话"),
            tr("“%1”的脚本不是可识别的文本：\n%2")
                .arg(npcName, scriptPath));
        return;
    }
    const QVector<DialogueReference> references =
        DialogueDocument::findLiteralTalkReferences(
            QString::fromUtf8(normalizedBytes));
    if (references.isEmpty())
    {
        QMessageBox::information(
            this, tr("没有可编辑的对话"),
            tr("“%1”的脚本中没有直接引用 talk(\"段落\")。你仍可打开脚本继续查找。")
                .arg(npcName));
        return;
    }

    int selectedIndex = 0;
    if (references.size() > 1)
    {
        QStringList choices;
        for (const DialogueReference& reference : references)
        {
            choices.append(tr("%1（脚本第 %2 行）")
                .arg(reference.section)
                .arg(reference.line));
        }
        bool accepted = false;
        const QString selected = QInputDialog::getItem(
            this, tr("选择要编辑的对话"),
            tr("“%1”的脚本引用了多段对话：").arg(npcName),
            choices, 0, false, &accepted);
        if (!accepted)
            return;
        selectedIndex = choices.indexOf(selected);
        if (selectedIndex < 0)
            return;
    }

    const DialogueReference& reference = references[selectedIndex];
    openDialogueReference(
        scriptPath, reference.section,
        reference.line, reference.column);
}

bool MainWindow::openDialogueReference(
    const QString& scriptPath, const QString& section,
    int line, int column)
{
    if (scriptPath.trimmed().isEmpty())
    {
        QMessageBox::information(
            this, tr("无法编辑对话"),
            tr("请先保存脚本，再从 talk(\"段落\") 调用进入对话。"));
        return false;
    }
    const QString talkPath = DialogueDocument::talkFileForScript(scriptPath);
    if (!QFileInfo(talkPath).isFile())
    {
        QMessageBox::information(
            this, tr("无法编辑对话"),
            tr("当前地图没有找到对话文件：\n%1").arg(talkPath));
        return false;
    }
    return openDialogueFile(
        talkPath, section, scriptPath, line, column);
}

bool MainWindow::openDialogueFile(
    const QString& requestedPath, const QString& section,
    const QString& callerScriptPath,
    int callerLine, int callerColumn)
{
    const QString normalized =
        EditorAssetPath::normalizedAbsolutePath(requestedPath);
    const QString key =
        ProjectDocumentRegistry::documentPathKey(normalized);
    QPointer<QMdiSubWindow> existing = documentWindows.value(key);
    if (!existing)
    {
        for (QMdiSubWindow* candidate : ui->mdiArea->subWindowList())
        {
            auto* dialogueCandidate =
                qobject_cast<DialogueEditorWindow*>(candidate->widget());
            if (dialogueCandidate &&
                ProjectDocumentRegistry::documentPathKey(
                    dialogueCandidate->currentFilePath()) == key)
            {
                existing = candidate;
                break;
            }
        }
    }
    if (existing)
    {
        auto* dialogueWindow =
            qobject_cast<DialogueEditorWindow*>(existing->widget());
        if (!dialogueWindow)
        {
            QMessageBox::information(
                this, tr("无法打开对话"),
                tr("这个对话文件已经在其他编辑视图中打开。"));
            return false;
        }
        if (!callerScriptPath.isEmpty())
        {
            dialogueWindow->setCaller(
                callerScriptPath, callerLine, callerColumn);
        }
        if (!section.isEmpty())
            dialogueWindow->selectSection(section);
        ui->mdiArea->setActiveSubWindow(existing);
        existing->show();
        addRecentFile(normalized);
        refreshPlaytestTargetPresentation();
        return true;
    }

    auto* window = new DialogueEditorWindow(this);
    window->setMinimumSize(1080, 700);
    const QString assetsPath = activeAssetsPath();
    if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
        window->setAssetsBasePath(assetsPath);
    window->setDocumentPathValidator(
        [this, window](const QString& currentPath,
                       const QString& targetPath)
        {
            return canDocumentWindowAdoptPath(
                window, currentPath, targetPath);
        });
    if (!window->openFile(normalized, section))
    {
        delete window;
        return false;
    }
    if (!callerScriptPath.isEmpty())
    {
        window->setCaller(
            callerScriptPath, callerLine, callerColumn);
    }
    QMdiSubWindow* subWindow = createMdiSubWindow(
        window, tr("对话 - %1").arg(window->displayName()));
    configureDialogueDocumentWindow(window, subWindow);
    window->show();
    addRecentFile(normalized);
    return true;
}

bool MainWindow::returnToDialogueCaller(
    const QString& scriptPath, int line, int column)
{
    if (!QFileInfo(scriptPath).isFile() || !openFileByType(scriptPath))
    {
        QMessageBox::information(
            this, tr("无法返回调用位置"),
            tr("调用脚本已经不存在或无法打开：\n%1")
                .arg(scriptPath));
        return false;
    }
    const QString key = ProjectDocumentRegistry::documentPathKey(
        EditorAssetPath::normalizedAbsolutePath(scriptPath));
    const QPointer<QMdiSubWindow> subWindow =
        documentWindows.value(key);
    auto* scriptWindow = subWindow
        ? qobject_cast<ScriptEditorWindow*>(subWindow->widget())
        : nullptr;
    if (!scriptWindow)
    {
        QMessageBox::information(
            this, tr("无法返回调用位置"),
            tr("调用文件当前不是脚本编辑视图。"));
        return false;
    }
    ui->mdiArea->setActiveSubWindow(subWindow);
    subWindow->show();
    return scriptWindow->goToSourceLocation(line, column);
}

bool MainWindow::openFileByType(const QString& fileName)
{
    if (projectAssetMigrationInProgress)
    {
        ui->statusBar->showMessage(
            tr("项目资源导入/转换期间不能打开内容文件。"), 3000);
        return false;
    }

    QFileInfo fileInfo(fileName);
    if (!fileInfo.exists())
    {
        ui->statusBar->showMessage(tr("文件不存在: %1").arg(fileName), 3000);
        return false;
    }

    if (fileInfo.isDir())
    {
        auto window = new BatchConvertWindow(this);
        window->setMinimumSize(700, 500);
        window->setSourceDirectory(fileName);
        createMdiSubWindow(window, tr("资源格式转换 - %1").arg(fileInfo.fileName()));
        window->show();
        addRecentFile(fileName);
        return true;
    }

    const QString normalizedFileName =
        EditorAssetPath::normalizedAbsolutePath(fileName);
    if (activateRegisteredDocument(normalizedFileName))
    {
        addRecentFile(normalizedFileName);
        return true;
    }

    const QString suffix = fileInfo.suffix().toLower();
    const bool hasMapExtension =
        suffix == QStringLiteral("map");
    const MapFileProbe mapProbe =
        hasMapExtension
        ? probeMapFile(normalizedFileName)
        : MapFileProbe();
    if (hasMapExtension && !mapProbe.readable)
    {
        const QString message =
            tr("无法读取地图文件: %1（%2）")
                .arg(
                    normalizedFileName,
                    mapProbe.errorString);
        statusLabel->setText(message);
        ui->statusBar->showMessage(
            message,
            8000);
        return false;
    }
    const bool supportedMap =
        mapProbe.supported;
    const bool textMapFallback =
        hasMapExtension && !supportedMap &&
        !mapProbe.binarySignature &&
        mapProbe.editableText;
    if (hasMapExtension && !supportedMap &&
        !textMapFallback)
    {
        const QString message = tr(
            "不支持的地图格式: %1（仅支持 MAP File Ver2.0/Ver3.0；"
            "文本 .map 文件可直接用文本编辑器打开）")
            .arg(normalizedFileName);
        statusLabel->setText(message);
        ui->statusBar->showMessage(message, 8000);
        return false;
    }

    std::string fileNameStr = fileName.toUtf8().toStdString();
    PicType picType = hasMapExtension
        ? PicType::None
        : Util::detectPicType(fileNameStr);

    if (picType != PicType::None)
    {
        auto window = new ImageEditorWindow(this);
        window->setMinimumSize(800, 600);

        QString assetsPath = activeAssetsPath();
        if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
        {
            window->setAssetsBasePath(assetsPath);
        }

        window->setDocumentPathValidator(
            [this, window](const QString& currentPath,
                           const QString& targetPath)
            {
                return canDocumentWindowAdoptPath(
                    window, currentPath, targetPath);
            });

        if (!window->openFile(fileName))
        {
            delete window;
            return false;
        }
        QMdiSubWindow* subWindow =
            createMdiSubWindow(window, fileInfo.fileName());
        configureImageDocumentWindow(window, subWindow);
        window->show();
        addRecentFile(fileName);
        return true;
    }
    else if (supportedMap)
    {
        auto window = new MapEditorWindow(this);
        window->setMinimumSize(1000, 700);

        QString assetsPath = activeAssetsPath();
        if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
        {
            window->setAssetsBasePath(assetsPath);
        }

        window->setDocumentPathValidator(
            [this, window](const QString& currentPath,
                           const QString& targetPath)
            {
                return canDocumentWindowAdoptPath(
                    window, currentPath, targetPath);
            });

        if (!window->openMapFile(fileName))
        {
            delete window;
            return false;
        }
        QMdiSubWindow* subWindow =
            createMdiSubWindow(window, fileInfo.fileName());
        configureMapDocumentWindow(window, subWindow);
        window->show();
        addRecentFile(fileName);
        return true;
    }
    else if (DialogueEditorWindow::isDialogueFilePath(normalizedFileName))
    {
        return openDialogueFile(normalizedFileName, QString());
    }
    else if (MagicEditorWindow::isMagicFilePath(normalizedFileName))
    {
        auto window = new MagicEditorWindow(this);
        window->setMinimumSize(1000, 700);

        const QString assetsPath = activeAssetsPath();
        if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
            window->setAssetsBasePath(assetsPath);

        window->setDocumentPathValidator(
            [this, window](const QString& currentPath,
                           const QString& targetPath)
            {
                return canDocumentWindowAdoptPath(
                    window, currentPath, targetPath);
            });

        if (!window->openFile(normalizedFileName))
        {
            delete window;
            return false;
        }
        QMdiSubWindow* subWindow = createMdiSubWindow(
            window, tr("武功 - %1").arg(fileInfo.completeBaseName()));
        configureMagicDocumentWindow(window, subWindow);
        window->show();
        addRecentFile(normalizedFileName);
        return true;
    }
    else if (GoodsShopEditorWindow::isSupportedFilePath(normalizedFileName))
    {
        auto window = new GoodsShopEditorWindow(this);
        window->setMinimumSize(1080, 720);

        const QString assetsPath = activeAssetsPath();
        if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
            window->setAssetsBasePath(assetsPath);

        window->setDocumentPathValidator(
            [this, window](const QString& currentPath,
                           const QString& targetPath)
            {
                return canDocumentWindowAdoptPath(
                    window, currentPath, targetPath);
            });

        if (!window->openFile(normalizedFileName))
        {
            delete window;
            return false;
        }
        const QString title = window->documentKind() ==
                GoodsShopDocumentKind::Goods
            ? tr("物品 - %1").arg(window->displayName())
            : tr("商店 - %1").arg(window->displayName());
        QMdiSubWindow* subWindow = createMdiSubWindow(window, title);
        configureGoodsShopDocumentWindow(window, subWindow);
        window->show();
        addRecentFile(normalizedFileName);
        return true;
    }
    else if (NpcDataEditorWindow::isNpcResourceFilePath(fileName))
    {
        auto window = new NpcDataEditorWindow(this);
        window->setMinimumSize(1000, 700);

        QString assetsPath = activeAssetsPath();
        if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
            window->setAssetsBasePath(assetsPath);

        window->setDocumentPathValidator(
            [this, window](const QString& currentPath,
                           const QString& targetPath)
            {
                return canDocumentWindowAdoptPath(
                    window, currentPath, targetPath);
            });

        if (!window->openNpcResourceFile(fileName))
        {
            delete window;
            return false;
        }
        QMdiSubWindow* subWindow =
            createMdiSubWindow(window, fileInfo.fileName());
        configureNpcDataDocumentWindow(window, subWindow);
        window->show();
        addRecentFile(fileName);
        return true;
    }
    else if (fileName.endsWith(".npc", Qt::CaseInsensitive) ||
             fileName.endsWith("_npc.ini", Qt::CaseInsensitive))
    {
        auto window = new NpcDataEditorWindow(this);
        window->setMinimumSize(900, 600);

        QString assetsPath = activeAssetsPath();
        if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
        {
            window->setAssetsBasePath(assetsPath);
        }

        window->setDocumentPathValidator(
            [this, window](const QString& currentPath,
                           const QString& targetPath)
            {
                return canDocumentWindowAdoptPath(
                    window, currentPath, targetPath);
            });

        if (!window->openNpcFile(fileName))
        {
            delete window;
            return false;
        }
        QMdiSubWindow* subWindow =
            createMdiSubWindow(window, fileInfo.fileName());
        configureNpcDataDocumentWindow(window, subWindow);
        window->show();
        addRecentFile(fileName);
        return true;
    }
    else if (fileName.endsWith(".obj", Qt::CaseInsensitive) ||
             fileName.endsWith("_obj.ini", Qt::CaseInsensitive))
    {
        auto window = new NpcDataEditorWindow(this);
        window->setMinimumSize(900, 600);

        QString assetsPath = activeAssetsPath();
        if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
        {
            window->setAssetsBasePath(assetsPath);
        }

        window->setDocumentPathValidator(
            [this, window](const QString& currentPath,
                           const QString& targetPath)
            {
                return canDocumentWindowAdoptPath(
                    window, currentPath, targetPath);
            });

        if (!window->openObjectFile(fileName))
        {
            delete window;
            return false;
        }
        QMdiSubWindow* subWindow =
            createMdiSubWindow(window, fileInfo.fileName());
        configureNpcDataDocumentWindow(window, subWindow);
        window->show();
        addRecentFile(fileName);
        return true;
    }
    else if (fileName.endsWith(".menu.ini", Qt::CaseInsensitive))
    {
        auto window = new MenuEditorWindow(this);
        window->setMinimumSize(1000, 600);

        QString assetsPath = activeAssetsPath();
        if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
        {
            window->setAssetsBasePath(assetsPath);
        }

        window->setDocumentPathValidator(
            [this, window](const QString& currentPath,
                           const QString& targetPath)
            {
                return canDocumentWindowAdoptPath(
                    window, currentPath, targetPath);
            });

        if (!window->openMenuDefinition(fileName))
        {
            delete window;
            return false;
        }
        QMdiSubWindow* subWindow =
            createMdiSubWindow(window, fileInfo.fileName());
        configureMenuDocumentWindow(window, subWindow);
        window->show();
        addRecentFile(fileName);
        return true;
    }
    else if (textMapFallback ||
             suffix == QStringLiteral("lua") ||
             suffix == QStringLiteral("ini") ||
             ScriptConverter::isScriptFile(fileNameStr))
    {
        const bool genericTextDocument =
            textMapFallback ||
            suffix == QStringLiteral("ini");
        auto window = new ScriptEditorWindow(
            this,
            genericTextDocument
                ? ScriptEditorWindow::EditingMode::
                      GenericText
                : ScriptEditorWindow::EditingMode::
                      Script);
        window->setMinimumSize(1000, 700);
        if (!window->openFile(normalizedFileName))
        {
            delete window;
            return false;
        }
        QMdiSubWindow* subWindow =
            createMdiSubWindow(window, fileInfo.fileName());
        configureScriptDocumentWindow(window, subWindow);
        window->show();
        addRecentFile(normalizedFileName);
        if (textMapFallback ||
            suffix == QStringLiteral("ini"))
        {
            ui->statusBar->showMessage(
                tr("已作为文本打开: %1")
                    .arg(normalizedFileName),
                5000);
        }
        return true;
    }
    else
    {
        ui->statusBar->showMessage(tr("不支持的文件格式: %1").arg(fileName), 3000);
        return false;
    }
}

void MainWindow::updateRecentFileActions()
{
    int numRecentFiles = qMin(recentFiles.size(), maxRecentFiles);
    const bool canOpenRecent =
        !projectAssetMigrationInProgress;

    for (int i = 0; i < numRecentFiles; ++i)
    {
        QString text = QString("&%1 %2").arg(i + 1).arg(QFileInfo(recentFiles[i]).fileName());
        recentFileActions[i]->setText(text);
        recentFileActions[i]->setData(recentFiles[i]);
        recentFileActions[i]->setStatusTip(recentFiles[i]);
        recentFileActions[i]->setVisible(true);
        recentFileActions[i]->setEnabled(
            canOpenRecent);
    }

    for (int i = numRecentFiles; i < maxRecentFiles; ++i)
    {
        recentFileActions[i]->setVisible(false);
    }

    ui->clearRecentAction->setEnabled(
        numRecentFiles > 0 && canOpenRecent);
}

void MainWindow::addRecentFile(const QString& fileName)
{
    if (restoringProjectDocumentSession)
        return;

    recentFiles.removeAll(fileName);
    recentFiles.prepend(fileName);

    while (recentFiles.size() > maxRecentFiles)
    {
        recentFiles.removeLast();
    }

    QSettings settings = createSettings();
    settings.setValue("recentFiles", recentFiles);

    if (ProjectManager::instance().isProjectOpen())
    {
        ProjectManager::instance().addRecentFile(fileName);
    }

    updateRecentFileActions();
}

void MainWindow::onBatchConvert()
{
    BatchConvertWindow* window = nullptr;
    QMdiSubWindow* subWindow = nullptr;
    for (auto sub : ui->mdiArea->subWindowList())
    {
        auto batchWin = qobject_cast<BatchConvertWindow*>(sub->widget());
        if (batchWin)
        {
            window = batchWin;
            subWindow = sub;
            break;
        }
    }

    const bool createdWindow = window == nullptr;
    if (createdWindow)
    {
        window = new BatchConvertWindow(this);
        window->setMinimumSize(700, 500);
    }

    if (ProjectManager::instance().isProjectOpen() &&
        !configureProjectAssetMigration(window))
    {
        if (createdWindow)
            delete window;
        else
            ui->mdiArea->setActiveSubWindow(subWindow);
        return;
    }

    if (createdWindow)
        subWindow = createMdiSubWindow(window, tr("资源格式转换"));
    ui->mdiArea->setActiveSubWindow(subWindow);
    window->show();
    statusLabel->setText(window->isProjectMigrationMode()
        ? tr("项目资源导入/转换") : tr("资源格式转换"));
}

void MainWindow::onExportAndroidExternalResource()
{
    const QString collectionRoot = resourceCollectionRoot();
    if (collectionRoot.trimmed().isEmpty() ||
        !QFileInfo(collectionRoot).isDir())
    {
        QMessageBox::information(
            this,
            tr("导出 Android 外部资源"),
            tr("请先打开项目或配置资源集合目录。"));
        return;
    }

    const ProjectManager& projectManager =
        ProjectManager::instance();
    const ResourcePackSelection selection =
        ResourcePackScanner::resolveActivePack(
            collectionRoot,
            projectManager.isProjectOpen()
                ? projectManager.activeResourcePackId()
                : QString(),
            projectManager.isProjectOpen()
                ? projectManager.activeResourcePackEntryKey()
                : QString());
    if (!selection.recoveryErrors.isEmpty())
    {
        QMessageBox::critical(
            this,
            tr("文件事务恢复失败"),
            selection.recoveryErrors.join('\n'));
        return;
    }
    if (!selection.isReady() ||
        selection.activePack.rootPath.isEmpty() ||
        !selection.activePack.profile.isValid())
    {
        QMessageBox::information(
            this,
            tr("没有可导出的活动资源包"),
            tr("请选择包含根级 game_profile.ini 和非空 Game.Id 的活动资源包后再导出。"));
        return;
    }

    AndroidExternalResourcePackageDialog dialog(
        selection.collectionRoot,
        selection.activePack,
        this);
    dialog.exec();
    statusLabel->setText(tr("Android 外部资源导出"));
}

bool MainWindow::configureProjectAssetMigration(
    BatchConvertWindow* window)
{
    auto& projectManager = ProjectManager::instance();
    if (!window || !projectManager.isProjectOpen())
        return false;

    if (projectManager.sourceAssetsRoot().trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("项目未配置原始资源根"),
            tr("请先在“项目设置”中选择只读原始资源根，再开始项目资源导入/转换。"));
        return false;
    }

    ResourcePackSelection selection;
    if (!resolveProjectResourceContext(
            projectManager.editableAssetsRoot(),
            projectManager.activeResourcePackId(),
            projectManager.activeResourcePackEntryKey(),
            false,
            selection))
    {
        return false;
    }

    ProjectAssetMigrationContext context;
    context.projectFilePath = projectManager.projectFilePath();
    context.sourceAssetsRoot = projectManager.sourceAssetsRoot();
    context.editableAssetsRoot = projectManager.editableAssetsRoot();
    context.activeContentRoot = selection.activeRoot;
    context.activeResourcePackId = selection.activeResourcePackId;
    context.activeResourcePackEntryKey =
        selection.activeResourcePackEntryKey;
    context.migrationOptions.legacyImages =
        projectManager.assetMigrationPolicy();

    if (!selection.activeResourcePackId.isEmpty())
    {
        const GameProfile& profile = selection.activePack.profile;
        context.migrationOptions.modId = selection.activeResourcePackId;
        context.migrationOptions.modName = profile.name;
        context.migrationOptions.modType =
            profile.typeDefined ? profile.type : -1;
        context.migrationOptions.dependencyId = profile.dependencyId;
        context.migrationOptions.saveNamespace = profile.saveNamespace;
        context.migrationOptions.minimumMagicDamage =
            profile.minimumMagicDamage;
        context.migrationOptions.minimumMagicDamageDefined =
            profile.minimumMagicDamageDefined;
        context.migrationOptions.defeatedNpcExperienceMode =
            profile.defeatedNpcExperienceMode;
        context.migrationOptions.defeatedNpcExperienceModeDefined =
            profile.defeatedNpcExperienceModeDefined;
        context.migrationOptions.experienceMultiplier =
            profile.experienceMultiplier;
        context.migrationOptions.experienceMultiplierDefined =
            profile.experienceMultiplierDefined;
        context.migrationOptions.levelUpThresholdMode =
            profile.levelUpThresholdMode;
        context.migrationOptions.levelUpThresholdModeDefined =
            profile.levelUpThresholdModeDefined;
        context.migrationOptions.partnerFollowRadius =
            profile.partnerFollowRadius;
        context.migrationOptions.partnerFollowRadiusDefined =
            profile.partnerFollowRadiusDefined;
        context.migrationOptions.partnerFollowRunRadius =
            profile.partnerFollowRunRadius;
        context.migrationOptions.partnerFollowRunRadiusDefined =
            profile.partnerFollowRunRadiusDefined;
        context.migrationOptions.npcActionProfile =
            profile.npcActionProfile;
        context.migrationOptions.npcActionProfileDefined =
            profile.npcActionProfileDefined;
        context.migrationOptions.npcRuntimeProfile =
            profile.npcRuntimeProfile;
        context.migrationOptions.npcRuntimeProfileDefined =
            profile.npcRuntimeProfileDefined;
        context.migrationOptions.specialActionMode =
            profile.specialActionMode;
        context.migrationOptions.specialActionModeDefined =
            profile.specialActionModeDefined;
        context.migrationOptions.addLifeMode = profile.addLifeMode;
        context.migrationOptions.addLifeModeDefined =
            profile.addLifeModeDefined;
        context.migrationOptions.titleMusic = profile.titleMusic;
        INIFileEditor activeManifest;
        context.migrationOptions.titleMusicDefined =
            !profile.manifestPath.isEmpty() &&
            activeManifest.loadFromFile(
                profile.manifestPath.toStdString()) &&
            activeManifest.hasKey("Title", "Music");
        context.migrationOptions.uiProfile = profile.uiProfile;
        context.migrationOptions.uiBaseId = profile.uiBaseId;
        context.migrationOptions.preferLocalUi = profile.preferLocalUi;
        context.migrationOptions.features = profile.features;
    }

    const bool configured = window->configureProjectMigration(
        context,
        [context](const LegacyImageMigrationPolicy& policy)
        {
            auto& currentProject = ProjectManager::instance();
            if (!currentProject.isProjectOpen() ||
                EditorAssetPath::comparisonKey(
                    currentProject.projectFilePath()) !=
                    EditorAssetPath::comparisonKey(context.projectFilePath) ||
                EditorAssetPath::comparisonKey(
                    currentProject.sourceAssetsRoot()) !=
                    EditorAssetPath::comparisonKey(context.sourceAssetsRoot) ||
                EditorAssetPath::comparisonKey(
                    currentProject.editableAssetsRoot()) !=
                    EditorAssetPath::comparisonKey(context.editableAssetsRoot))
            {
                return;
            }
            currentProject.setAssetMigrationPolicy(policy);
        },
        [this, context](QString& errorMessage)
        {
            return beginProjectAssetMigration(context, errorMessage);
        },
        [this, context](bool published, QString& errorMessage)
        {
            return finishProjectAssetMigration(
                context, published, errorMessage);
        });
    if (!configured)
    {
        QMessageBox::warning(this, tr("项目导入正在进行"),
            tr("当前转换窗口仍在工作，不能重新绑定项目资源上下文。"));
    }
    return configured;
}

bool MainWindow::beginProjectAssetMigration(
    const ProjectAssetMigrationContext& context,
    QString& errorMessage)
{
    auto& projectManager = ProjectManager::instance();
    const auto samePath = [](const QString& first, const QString& second)
    {
        return EditorAssetPath::comparisonKey(first) ==
            EditorAssetPath::comparisonKey(second);
    };

    if (projectAssetMigrationInProgress)
    {
        errorMessage = tr("已有项目资源导入正在进行。");
        return false;
    }
    if (!projectManager.isProjectOpen() ||
        !samePath(projectManager.projectFilePath(), context.projectFilePath) ||
        !samePath(projectManager.sourceAssetsRoot(), context.sourceAssetsRoot) ||
        !samePath(projectManager.editableAssetsRoot(), context.editableAssetsRoot) ||
        !samePath(activeAssetsPath(), context.activeContentRoot) ||
        projectManager.activeResourcePackId().compare(
            context.activeResourcePackId, Qt::CaseInsensitive) != 0 ||
        projectManager.activeResourcePackEntryKey().compare(
            context.activeResourcePackEntryKey,
            Qt::CaseInsensitive) != 0)
    {
        errorMessage = tr("项目、资源根或活动资源包已改变，请重新打开项目导入入口。");
        return false;
    }

    if (!documentRegistry.documents().isEmpty())
    {
        errorMessage = tr("请先关闭当前项目的全部已打开文档，再开始替换活动内容根。");
        return false;
    }
    for (QMdiSubWindow* subWindow : ui->mdiArea->subWindowList())
    {
        if (dynamic_cast<AssetsPathSwitchParticipant*>(subWindow->widget()))
        {
            errorMessage = tr("请先关闭所有内容或资源编辑窗口，再开始项目资源导入。");
            return false;
        }
    }

    setProjectAssetMigrationInProgress(true);
    statusLabel->setText(tr("正在导入/转换项目资源，请勿切换项目或打开内容文档"));
    return true;
}

bool MainWindow::finishProjectAssetMigration(
    const ProjectAssetMigrationContext& context,
    bool published,
    QString& errorMessage)
{
    setProjectAssetMigrationInProgress(false);
    if (!published)
    {
        statusLabel->setText(tr("项目资源导入未发布，项目保持不变"));
        return true;
    }

    auto& projectManager = ProjectManager::instance();
    const auto samePath = [](const QString& first, const QString& second)
    {
        return EditorAssetPath::comparisonKey(first) ==
            EditorAssetPath::comparisonKey(second);
    };
    if (!projectManager.isProjectOpen() ||
        !samePath(projectManager.projectFilePath(), context.projectFilePath) ||
        !samePath(projectManager.sourceAssetsRoot(), context.sourceAssetsRoot) ||
        !samePath(projectManager.editableAssetsRoot(), context.editableAssetsRoot))
    {
        errorMessage = tr("转换期间项目上下文发生变化；输出已保留，但项目没有自动切换。");
        return false;
    }

    ProjectResourceConfiguration configuration;
    configuration.sourceAssetsRoot = context.sourceAssetsRoot;
    configuration.editableAssetsRoot = context.editableAssetsRoot;
    configuration.activeResourcePackId = context.activeResourcePackId;
    configuration.activeResourcePackEntryKey =
        context.activeResourcePackEntryKey;
    if (!applyProjectResourceConfiguration(configuration))
    {
        errorMessage = tr("重新解析转换输出或保存项目资源设置失败。");
        return false;
    }

    statusLabel->setText(tr("项目资源已导入并重新加载"));
    return true;
}

void MainWindow::setProjectAssetMigrationInProgress(bool inProgress)
{
    projectAssetMigrationInProgress = inProgress;
    const bool enabled = !inProgress;
    ui->newProjectAction->setEnabled(enabled);
    ui->openProjectAction->setEnabled(enabled);
    ui->saveProjectAction->setEnabled(enabled);
    ui->projectSettingsAction->setEnabled(
        enabled && ProjectManager::instance().isProjectOpen());
    ui->runtimeConfigurationAction->setEnabled(
        enabled && ProjectManager::instance().isProjectOpen());
    ui->openImageEditorAction->setEnabled(enabled);
    ui->openMapEditorAction->setEnabled(enabled);
    ui->openMenuEditorAction->setEnabled(enabled);
    ui->openScriptEditorAction->setEnabled(enabled);
    ui->openNpcDataEditorAction->setEnabled(enabled);
    ui->openResourceProfileEditorAction->setEnabled(enabled);
    ui->exportAndroidExternalResourceAction->setEnabled(enabled);
    ui->scriptProjectSearchAction->setEnabled(
        enabled && ProjectManager::instance().isProjectOpen());
    ui->setAssetsPathAction->setEnabled(enabled);
    if (projectExplorerDock)
        projectExplorerDock->setEnabled(enabled);
    if (desktopRunPanel)
    {
        desktopRunPanel->
            setAuthoringOperationInProgress(
                inProgress);
    }
    updateRecentFileActions();
    updateDesktopRunActionStates();
}

void MainWindow::onOpenImageEditor()
{
    for (auto sub : ui->mdiArea->subWindowList())
    {
        auto imageWin = qobject_cast<ImageEditorWindow*>(sub->widget());
        if (imageWin)
        {
            ui->mdiArea->setActiveSubWindow(sub);
            return;
        }
    }

    auto window = new ImageEditorWindow(this);
    window->setMinimumSize(800, 600);

    QString assetsPath = activeAssetsPath();
    if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
    {
        window->setAssetsBasePath(assetsPath);
    }

    QMdiSubWindow* subWindow =
        createMdiSubWindow(window, tr("图片编辑器"));
    configureImageDocumentWindow(window, subWindow);
    window->show();
    statusLabel->setText(tr("图片编辑器"));
}

void MainWindow::onOpenMapEditor()
{
    auto window = new MapEditorWindow(this);
    window->setMinimumSize(1000, 700);

    QString assetsPath = activeAssetsPath();
    if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
    {
        window->setAssetsBasePath(assetsPath);
    }

    QMdiSubWindow* subWindow =
        createMdiSubWindow(window, tr("地图编辑器"));
    configureMapDocumentWindow(window, subWindow);
    window->show();
    statusLabel->setText(tr("地图编辑器"));
}

void MainWindow::onOpenMenuEditor()
{
    for (auto sub : ui->mdiArea->subWindowList())
    {
        auto menuWin = qobject_cast<MenuEditorWindow*>(sub->widget());
        if (menuWin)
        {
            ui->mdiArea->setActiveSubWindow(sub);
            return;
        }
    }

    auto window = new MenuEditorWindow(this);
    window->setMinimumSize(1000, 600);

    QString assetsPath = activeAssetsPath();
    if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
    {
        window->setAssetsBasePath(assetsPath);
    }

    QMdiSubWindow* subWindow =
        createMdiSubWindow(window, tr("菜单编辑器"));
    configureMenuDocumentWindow(window, subWindow);
    window->show();
    statusLabel->setText(tr("菜单编辑器"));
}

void MainWindow::onOpenScriptEditor()
{
    for (auto sub : ui->mdiArea->subWindowList())
    {
        auto scriptWin = qobject_cast<ScriptEditorWindow*>(sub->widget());
        if (scriptWin &&
            scriptWin->isRunnableScript())
        {
            ui->mdiArea->setActiveSubWindow(sub);
            return;
        }
    }

    auto window = new ScriptEditorWindow(this);
    window->setMinimumSize(1000, 700);

    QMdiSubWindow* subWindow =
        createMdiSubWindow(window, tr("脚本编辑器"));
    configureScriptDocumentWindow(window, subWindow);
    window->show();
    statusLabel->setText(tr("脚本编辑器"));
}

void MainWindow::onOpenNpcDataEditor()
{
    auto window = new NpcDataEditorWindow(this);
    window->setMinimumSize(1000, 700);

    QString assetsPath = activeAssetsPath();
    if (!assetsPath.isEmpty() && QDir(assetsPath).exists())
    {
        window->setAssetsBasePath(assetsPath);
    }

    QMdiSubWindow* subWindow =
        createMdiSubWindow(window, tr("NPC/OBJ/资源编辑器"));
    configureNpcDataDocumentWindow(window, subWindow);
    window->show();
    statusLabel->setText(tr("NPC/OBJ/资源编辑器"));
}

void MainWindow::onOpenResourceProfileEditor()
{
    auto window = new ResourceProfileEditorWindow(this);
    window->setMinimumSize(600, 700);
    connect(window, &ResourceProfileEditorWindow::activateResourcePackRequested,
        this, [this](
            const QString& resourcePackId,
            const QString& resourcePackEntryKey)
        {
            requestActiveResourcePackChange(
                resourcePackId,
                resourcePackEntryKey);
        });
    connect(window, &ResourceProfileEditorWindow::resourcePackIdentityChanged,
        this, [this](const QString& previousId, const QString& currentId,
                    const QString& resourcePackRoot)
        {
            const ProjectManager& projectManager = ProjectManager::instance();
            if (!projectManager.isProjectOpen())
            {
                return;
            }

            const ResourcePackSelection activeSelection =
                ResourcePackScanner::resolveActivePack(
                    projectManager.editableAssetsRoot(),
                    projectManager.activeResourcePackId(),
                    projectManager.activeResourcePackEntryKey());
            if (activeSelection.isReady() &&
                EditorAssetPath::comparisonKey(
                    activeSelection.activeRoot) ==
                    EditorAssetPath::comparisonKey(
                        resourcePackRoot))
            {
                requestActiveResourcePackChange(
                    currentId,
                    activeSelection.activeResourcePackEntryKey);
            }
            else if (projectManager.activeResourcePackEntryKey().isEmpty() &&
                     projectManager.activeResourcePackId().compare(
                         previousId, Qt::CaseInsensitive) == 0)
            {
                requestActiveResourcePackChange(currentId);
            }
        });

    const ProjectManager& projectManager = ProjectManager::instance();
    const QString collectionRoot = resourceCollectionRoot();
    if (!collectionRoot.isEmpty() && QDir(collectionRoot).exists())
    {
        window->setAssetsBasePath(collectionRoot);
    }
    window->setProjectResourceContext(
        projectManager.activeResourcePackId(),
        projectManager.isProjectOpen(),
        projectManager.activeResourcePackEntryKey());

    createMdiSubWindow(window, tr("MOD 发布与资源设置"));
    window->show();
    statusLabel->setText(tr("MOD 发布与资源设置"));
}

void MainWindow::onFindAssetReferences()
{
    const QString contentRoot = activeAssetsPath();
    if (contentRoot.isEmpty() || !QFileInfo(contentRoot).isDir())
    {
        QMessageBox::information(this, tr("资源引用搜索"),
            tr("请先打开项目或配置可用的资源目录。"));
        return;
    }

    const ResourceContentRootResolution resolution =
        ResourcePackScanner::resolveContentRoots(contentRoot);
    if (!resolution.recoveryErrors.isEmpty())
    {
        QMessageBox::critical(
            this,
            tr("文件事务恢复失败"),
            tr("资源目录中存在未能安全恢复的保存事务，无法枚举资源引用：\n%1")
                .arg(resolution.recoveryErrors.join('\n')));
        return;
    }
    QProgressDialog progress(
        tr("正在枚举资源引用..."), tr("取消"), 0, 0, this);
    progress.setObjectName(QStringLiteral("assetReferenceProgressDialog"));
    progress.setWindowTitle(tr("资源引用搜索"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();
    QApplication::processEvents();

    QElapsedTimer eventPumpTimer;
    eventPumpTimer.start();
    AssetReferenceScanReport report = AssetReferenceScanner::scan(
        contentRoot, resolution.roots,
        [&progress](int current, int total, const QString& currentFile)
        {
            progress.setRange(0, total);
            progress.setValue(current);
            progress.setLabelText(tr("正在扫描 %1\n%2 / %3")
                .arg(currentFile).arg(current).arg(total));
            QApplication::processEvents();
        },
        [&progress, &eventPumpTimer]()
        {
            if (eventPumpTimer.elapsed() >= 16)
            {
                QApplication::processEvents();
                eventPumpTimer.restart();
            }
            return progress.wasCanceled();
        });
    progress.close();

    if (report.cancelled)
    {
        statusLabel->setText(tr("资源引用搜索已取消；项目和文件均未修改。"));
        return;
    }

    for (const QString& missingId : resolution.missingDependencyIds)
    {
        AssetReferenceScanIssue issue;
        issue.message = tr("内容依赖 ID 不可用：%1").arg(missingId);
        report.issues.append(issue);
    }
    for (const QString& missingPath : resolution.missingPaths)
    {
        AssetReferenceScanIssue issue;
        issue.message = tr("内容依赖路径不可用：%1").arg(missingPath);
        report.issues.append(issue);
    }

    AssetReferenceDialog dialog(report, this);
    if (dialog.exec() == QDialog::Accepted &&
        !dialog.selectedSourceFile().isEmpty())
    {
        if (!openFileByType(dialog.selectedSourceFile()))
        {
            QMessageBox::information(this, tr("资源引用搜索"),
                tr("来源文件已定位，但当前编辑器没有可用的文件视图：\n%1")
                    .arg(dialog.selectedSourceFile()));
        }
        else if (dialog.selectedLineNumber() > 0)
        {
            const QString sourceKey =
                ProjectDocumentRegistry::documentPathKey(
                    dialog.selectedSourceFile());
            for (QMdiSubWindow* subWindow : ui->mdiArea->subWindowList())
            {
                auto* scriptWindow =
                    qobject_cast<ScriptEditorWindow*>(subWindow->widget());
                if (scriptWindow &&
                    ProjectDocumentRegistry::documentPathKey(
                        scriptWindow->currentFilePath()) ==
                        sourceKey)
                {
                    scriptWindow->goToLine(dialog.selectedLineNumber());
                    break;
                }
            }
        }
    }
    statusLabel->setText(tr("资源引用搜索完成：静态引用 %1，缺失 %2，问题 %3。")
        .arg(report.staticReferences)
        .arg(report.missingReferences)
        .arg(report.issues.size()));
}

void MainWindow::onFindProjectScripts()
{
    const ProjectManager& projectManager = ProjectManager::instance();
    if (!projectManager.isProjectOpen())
    {
        QMessageBox::information(this, tr("项目脚本搜索与替换"),
            tr("请先打开项目；项目脚本搜索不会使用独立的全局资源目录。"));
        return;
    }

    const QString contentRoot = activeAssetsPath();
    if (contentRoot.isEmpty() || !QFileInfo(contentRoot).isDir())
    {
        QMessageBox::warning(this, tr("项目脚本搜索与替换"),
            tr("当前项目的活动内容根不可用。"));
        return;
    }

    QSet<QString> dirtyDocumentPaths;
    for (const ProjectDocumentState& document : documentRegistry.documents())
    {
        if (document.dirty)
            dirtyDocumentPaths.insert(document.filePath);
    }

    ScriptProjectSearchDialog dialog(
        contentRoot, dirtyDocumentPaths, this);
    dialog.exec();
    const ScriptProjectReplaceResult result = dialog.publishResult();
    if (result.success)
    {
        QSet<QString> replacedPathKeys;
        for (const QString& filePath : result.replacedFilePaths)
        {
            replacedPathKeys.insert(
                ProjectDocumentRegistry::documentPathKey(filePath));
        }
        QStringList reloadFailures;
        for (QMdiSubWindow* subWindow : ui->mdiArea->subWindowList())
        {
            auto* scriptWindow =
                qobject_cast<ScriptEditorWindow*>(subWindow->widget());
            if (!scriptWindow ||
                !replacedPathKeys.contains(
                    ProjectDocumentRegistry::documentPathKey(
                        scriptWindow->currentFilePath())))
            {
                continue;
            }
            if (!scriptWindow->reloadFromDiskIfClean())
                reloadFailures.append(scriptWindow->currentFilePath());
        }

        QString message = tr("项目脚本事务替换完成：文件 %1，匹配 %2。")
            .arg(result.replacedFiles).arg(result.replacements);
        if (!result.warningMessage.isEmpty())
            message += tr(" 警告：%1").arg(result.warningMessage);
        if (!reloadFailures.isEmpty())
        {
            message += tr(" %1 个已打开文档未能重新载入，请手动检查。")
                .arg(reloadFailures.size());
        }
        statusLabel->setText(message);
    }
}

void MainWindow::onSetAssetsPath()
{
    if (projectAssetMigrationInProgress)
        return;

    const QString currentPath = resourceCollectionRoot();

    QString path = QFileDialog::getExistingDirectory(
        this,
        tr("选择游戏资源目录"),
        currentPath,
        QFileDialog::ShowDirsOnly |
            QFileDialog::DontResolveSymlinks);

    if (!path.isEmpty())
        requestAssetsPathChange(path);
}

void MainWindow::onFileAssociations()
{
    FileAssociationDialog dialog(
        this, &desktopFileAssociationManager,
        QCoreApplication::applicationFilePath());
    dialog.exec();
}

void MainWindow::repairDesktopFileAssociations()
{
#ifdef Q_OS_WIN
    const DesktopFileAssociationResult associationRepair =
        desktopFileAssociationManager.repairManagedAssociations(
            QCoreApplication::applicationFilePath());
    if (!associationRepair.success)
    {
        statusLabel->setText(tr("文件关联启动路径修复失败：%1")
                                 .arg(associationRepair.error));
    }
#endif
}

bool MainWindow::requestAssetsPathChange(const QString& path)
{
    if (projectAssetMigrationInProgress)
        return false;

    auto& projectManager = ProjectManager::instance();
    QString activeContentRoot = path;
    QString activeResourcePackId;
    QString activeResourcePackEntryKey;
    if (projectManager.isProjectOpen())
    {
        ResourcePackSelection selection;
        if (!resolveProjectResourceContext(path,
                projectManager.activeResourcePackId(),
                projectManager.activeResourcePackEntryKey(),
                true,
                selection))
        {
            return false;
        }
        activeContentRoot = selection.activeRoot;
        activeResourcePackId = selection.activeResourcePackId;
        activeResourcePackEntryKey =
            selection.activeResourcePackEntryKey;
    }

    if (!recoverFileTransactions(activeContentRoot))
        return false;

    QList<PreparedAssetsPathSwitch> prepared;
    if (!prepareAssetsPathChange(
            activeContentRoot,
            path,
            prepared))
    {
        return false;
    }
    if (!persistAssetsPathSetting(path))
    {
        // The graph is the last resolve participant and may have released
        // its derived root context. Restore analysis against the unchanged
        // project/resource selection when the settings commit fails.
        if (storyGraphWindow)
        {
            refreshStoryGraphAnalysis(false);
            synchronizeStoryGraphRuntimeTraceSession();
        }
        return false;
    }
    commitAssetsPathChange(prepared);
    if (projectManager.isProjectOpen())
    {
        projectManager.setEditableAssetsRoot(path);
        projectManager.setActiveResourcePackId(activeResourcePackId);
        projectManager.setActiveResourcePackEntryKey(
            activeResourcePackEntryKey);
        updateResourceProfileEditors();
    }
    refreshProjectWorkspace();
    statusLabel->setText(path.isEmpty()
        ? tr("资源目录已清除")
        : tr("资源目录已设置: %1").arg(path));
    return true;
}

bool MainWindow::requestActiveResourcePackChange(
    const QString& resourcePackId,
    const QString& resourcePackEntryKey)
{
    if (projectAssetMigrationInProgress)
        return false;

    auto& projectManager = ProjectManager::instance();
    if (!projectManager.isProjectOpen())
        return false;

    ResourcePackSelection selection;
    if (!resolveProjectResourceContext(projectManager.editableAssetsRoot(),
            resourcePackId,
            resourcePackEntryKey,
            false,
            selection) ||
        !selection.hasActivePack())
    {
        return false;
    }
    if (!recoverFileTransactions(selection.activeRoot))
        return false;

    QList<PreparedAssetsPathSwitch> prepared;
    if (!prepareAssetsPathChange(selection.activeRoot,
            selection.collectionRoot, prepared))
    {
        return false;
    }

    commitAssetsPathChange(prepared);
    projectManager.setActiveResourcePackId(selection.activeResourcePackId);
    projectManager.setActiveResourcePackEntryKey(
        selection.activeResourcePackEntryKey);
    updateResourceProfileEditors();
    refreshProjectWorkspace();
    statusLabel->setText(tr("项目活动资源包已设置: %1")
        .arg(selection.activeResourcePackId));
    return true;
}

bool MainWindow::recoverFileTransactions(const QString& assetsPath)
{
    if (assetsPath.isEmpty() || !QFileInfo(assetsPath).isDir())
        return true;

    QStringList errors;
    if (DurableFileTransaction::recoverPending(assetsPath, errors))
        return true;

    QMessageBox::critical(this, tr("文件事务恢复失败"),
        tr("资源目录中存在未能安全恢复的保存事务。为避免覆盖可恢复数据，"
           "编辑器不会切换到该资源目录：\n%1")
            .arg(errors.join("\n")));
    return false;
}

bool MainWindow::resolveProjectResourceContext(
    const QString& collectionRoot,
    const QString& requestedResourcePackId,
    const QString& requestedResourcePackEntryKey,
    bool allowUserSelection,
    ResourcePackSelection& selection)
{
    selection = ResourcePackScanner::resolveActivePack(
        collectionRoot,
        requestedResourcePackId,
        requestedResourcePackEntryKey);
    if (selection.isReady())
        return true;

    if (selection.status == ResourcePackSelectionStatus::InvalidAssetsRoot)
    {
        QMessageBox::warning(this, tr("资源目录无效"),
            tr("可编辑资源目录不存在或不可访问：\n%1").arg(collectionRoot));
        return false;
    }
    if (selection.status == ResourcePackSelectionStatus::RecoveryFailed)
    {
        QMessageBox::critical(
            this,
            tr("文件事务恢复失败"),
            tr("资源目录中存在未能安全恢复的保存事务。为避免覆盖可恢复数据，"
               "项目资源上下文不会切换：\n%1")
                .arg(selection.recoveryErrors.join('\n')));
        return false;
    }
    if (selection.status ==
        ResourcePackSelectionStatus::ResourceIdConflict)
    {
        QMessageBox::warning(
            this,
            tr("资源 ID 冲突"),
            tr("当前资源集合存在重复 Game.Id。请先修改其中一个资源的 ID，"
               "再进入或编辑该资源。"));
        return false;
    }
    if (!allowUserSelection)
    {
        QMessageBox::warning(this, tr("活动资源包无效"),
            tr("项目无法按 ID“%1”定位活动资源包。")
                .arg(requestedResourcePackId));
        return false;
    }

    if (selection.status == ResourcePackSelectionStatus::ActivePackNotFound)
    {
        QMessageBox::information(this, tr("重新选择活动资源包"),
            tr("项目记录的活动资源包“%1”已不在当前集合中，请明确选择替代包。")
                .arg(requestedResourcePackId));
    }

    QStringList labels;
    for (const ResourcePackInfo& pack : selection.availablePacks)
    {
        const QString name = pack.profile.name.trimmed().isEmpty()
            ? QFileInfo(pack.rootPath).fileName() : pack.profile.name.trimmed();
        labels.append(tr("%1 — %2 [%3]")
            .arg(name, pack.profile.id, QDir(collectionRoot)
                .relativeFilePath(pack.rootPath)));
    }
    if (labels.isEmpty())
        return false;

    bool accepted = false;
    const QString chosenLabel = QInputDialog::getItem(this,
        tr("选择项目活动资源包"),
        tr("内容编辑器将从所选资源包根加载和保存文件："),
        labels, 0, false, &accepted);
    if (!accepted)
        return false;

    const int chosenIndex = labels.indexOf(chosenLabel);
    if (chosenIndex < 0 || chosenIndex >= selection.availablePacks.size())
        return false;
    const ResourcePackInfo& chosenPack =
        selection.availablePacks[chosenIndex];
    return resolveProjectResourceContext(
        collectionRoot,
        chosenPack.profile.id,
        chosenPack.stableEntryKey,
        false,
        selection);
}

bool MainWindow::validateProjectResourceConfiguration(
    const QString& projectFilePath,
    const ProjectResourceConfiguration& resourceConfiguration,
    ProjectResourceConfiguration& resolvedConfiguration,
    ResourcePackSelection& selection)
{
    const QDir projectDirectory(
        QFileInfo(projectFilePath).absolutePath());
    auto resolvePath = [&projectDirectory](const QString& path)
    {
        const QString trimmedPath = path.trimmed();
        if (trimmedPath.isEmpty())
            return QString();
        const QString portablePath =
            QDir::fromNativeSeparators(trimmedPath);
        return EditorAssetPath::normalizedAbsolutePath(
            QDir::isAbsolutePath(portablePath)
                ? portablePath
                : projectDirectory.filePath(portablePath));
    };

    resolvedConfiguration = resourceConfiguration;
    resolvedConfiguration.sourceAssetsRoot =
        resolvePath(resourceConfiguration.sourceAssetsRoot);
    resolvedConfiguration.editableAssetsRoot =
        resolvePath(resourceConfiguration.editableAssetsRoot);
    resolvedConfiguration.activeResourcePackId =
        resourceConfiguration.activeResourcePackId.trimmed();
    resolvedConfiguration.activeResourcePackEntryKey =
        resourceConfiguration.activeResourcePackEntryKey.trimmed();

    if (!resolvedConfiguration.sourceAssetsRoot.isEmpty())
    {
        const QFileInfo sourceInfo(
            resolvedConfiguration.sourceAssetsRoot);
        if (!sourceInfo.isDir() || !sourceInfo.isReadable())
        {
            QMessageBox::warning(this, tr("项目设置无效"),
                tr("原始资源根不存在或不可读取：\n%1")
                    .arg(resolvedConfiguration.sourceAssetsRoot));
            return false;
        }
    }

    const QFileInfo editableInfo(
        resolvedConfiguration.editableAssetsRoot);
    if (resolvedConfiguration.editableAssetsRoot.isEmpty() ||
        !editableInfo.isDir() || !editableInfo.isReadable())
    {
        QMessageBox::warning(this, tr("项目设置无效"),
            tr("可编辑资源根不存在或不可读取：\n%1")
                .arg(resolvedConfiguration.editableAssetsRoot));
        return false;
    }

    if (!resolvedConfiguration.sourceAssetsRoot.isEmpty() &&
        EditorAssetPath::comparisonKey(
            resolvedConfiguration.sourceAssetsRoot) ==
        EditorAssetPath::comparisonKey(
            resolvedConfiguration.editableAssetsRoot))
    {
        QMessageBox::warning(this, tr("项目设置无效"),
            tr("原始资源根与可编辑资源根必须分离，不能选择同一目录。"));
        return false;
    }

    return resolveProjectResourceContext(
        resolvedConfiguration.editableAssetsRoot,
        resolvedConfiguration.activeResourcePackId,
        resolvedConfiguration.activeResourcePackEntryKey,
        false,
        selection);
}

void MainWindow::updateResourceProfileEditors()
{
    const ProjectManager& projectManager = ProjectManager::instance();
    for (QMdiSubWindow* subWindow : ui->mdiArea->subWindowList())
    {
        auto* resourceProfileEditor =
            qobject_cast<ResourceProfileEditorWindow*>(subWindow->widget());
        if (resourceProfileEditor)
        {
            resourceProfileEditor->setProjectResourceContext(
                projectManager.activeResourcePackId(),
                projectManager.isProjectOpen(),
                projectManager.activeResourcePackEntryKey());
        }
    }
}

bool MainWindow::prepareAssetsPathChange(
    const QString& activeContentRoot,
    const QString& resourceCollectionRoot,
    QList<PreparedAssetsPathSwitch>& prepared)
{
    prepared.clear();
    QList<AssetsPathSwitchParticipant*> participants;
    AssetsPathSwitchParticipant* deferredStoryGraph =
        nullptr;
    for (QMdiSubWindow* subWindow : ui->mdiArea->subWindowList())
    {
        auto* participant =
            dynamic_cast<AssetsPathSwitchParticipant*>(
                subWindow->widget());
        if (!participant)
            continue;
        if (storyGraphWindow &&
            participant == storyGraphWindow.data())
        {
            deferredStoryGraph = participant;
            continue;
        }
        participants.append(participant);
    }
    // The graph owns only derived state and logical resource-root paths. Resolve it last so
    // a failure in an authoring participant cannot leave a cancelled graph
    // after the overall resource switch was abandoned.
    if (deferredStoryGraph)
        participants.append(deferredStoryGraph);

    for (AssetsPathSwitchParticipant* participant :
         participants)
    {
        const QString targetPath = participant->assetsPathScope() ==
                AssetsPathSwitchParticipant::PathScope::ResourceCollectionRoot
            ? resourceCollectionRoot : activeContentRoot;
        const QString originalPath = participant->currentAssetsPath();
        const auto decision = participant->prepareAssetsPathSwitch(targetPath);
        if (decision == AssetsPathSwitchParticipant::Decision::Cancelled ||
            participant->currentAssetsPath() != originalPath)
        {
            return false;
        }
        prepared.append({participant, decision, originalPath, targetPath});
    }

    for (const PreparedAssetsPathSwitch& item : prepared)
    {
        if (item.participant->currentAssetsPath() != item.originalPath)
            return false;
        if (!item.participant->resolveAssetsPathSwitch(item.decision))
            return false;
    }

    return true;
}

bool MainWindow::persistAssetsPathSetting(const QString& path)
{
    QSettings settings = createSettings();
    const QString previousPath = settings.value("assetsPath").toString();
    settings.setValue("assetsPath", path);
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        settings.setValue("assetsPath", previousPath);
        settings.sync();
        QMessageBox::warning(this, tr("设置失败"),
            tr("无法保存资源目录设置，所有编辑器仍使用原目录。"));
        return false;
    }
    return true;
}

void MainWindow::commitAssetsPathChange(
    const QList<PreparedAssetsPathSwitch>& prepared)
{
    for (const PreparedAssetsPathSwitch& item : prepared)
    {
        Q_ASSERT(item.participant->currentAssetsPath() == item.originalPath);
        item.participant->commitAssetsPathSwitch(item.targetPath);
    }
}

void MainWindow::onOpenAboutDialog()
{
    AboutDialog dialog(this);
    dialog.exec();
}

QAction* MainWindow::activeDocumentEditAction(
    bool undo) const
{
    QMdiSubWindow* activeSubWindow =
        ui->mdiArea->activeSubWindow();
    QWidget* documentWidget =
        activeSubWindow
        ? activeSubWindow->widget()
        : nullptr;
    if (!documentWidget)
        return nullptr;

    const QString commandName =
        undo
        ? QStringLiteral("undo")
        : QStringLiteral("redo");
    const QKeySequence::StandardKey standardKey =
        undo
        ? QKeySequence::Undo
        : QKeySequence::Redo;
    const QList<QKeySequence> standardBindings =
        QKeySequence::keyBindings(standardKey);
    const QKeySequence legacyBinding(
        undo
        ? QStringLiteral("Ctrl+Z")
        : QStringLiteral("Ctrl+Y"));

    QAction* disabledCandidate = nullptr;
    const QList<QAction*> actions =
        documentWidget->findChildren<QAction*>();
    for (QAction* action : actions)
    {
        bool matches =
            action->objectName().contains(
                commandName,
                Qt::CaseInsensitive);
        if (!matches)
        {
            const QList<QKeySequence> shortcuts =
                action->shortcuts();
            for (const QKeySequence& shortcut : shortcuts)
            {
                if (shortcut == legacyBinding ||
                    standardBindings.contains(shortcut))
                {
                    matches = true;
                    break;
                }
            }
        }
        if (!matches)
            continue;
        if (action->isEnabled())
            return action;
        if (!disabledCandidate)
            disabledCandidate = action;
    }
    return disabledCandidate;
}

QUndoStack* MainWindow::activeDocumentUndoStack() const
{
    QMdiSubWindow* activeSubWindow =
        ui->mdiArea->activeSubWindow();
    QWidget* documentWidget =
        activeSubWindow
        ? activeSubWindow->widget()
        : nullptr;
    return documentWidget
        ? documentWidget->findChild<QUndoStack*>()
        : nullptr;
}

QWidget* MainWindow::activeDocumentFocusTarget() const
{
    QMdiSubWindow* activeSubWindow =
        ui->mdiArea->activeSubWindow();
    QWidget* documentWidget =
        activeSubWindow
        ? activeSubWindow->widget()
        : nullptr;
    if (!documentWidget)
        return nullptr;

    QWidget* currentFocusWidget =
        QApplication::focusWidget();
    if (currentFocusWidget &&
        (currentFocusWidget == documentWidget ||
         documentWidget->isAncestorOf(currentFocusWidget)))
    {
        return currentFocusWidget;
    }
    if (lastDocumentFocusWidget &&
        (lastDocumentFocusWidget == documentWidget ||
         documentWidget->isAncestorOf(
             lastDocumentFocusWidget)))
    {
        return lastDocumentFocusWidget;
    }
    return nullptr;
}

bool MainWindow::canExecuteActiveDocumentEditCommand(
    bool undo) const
{
    if (QUndoStack* undoStack = activeDocumentUndoStack())
    {
        return undo
            ? undoStack->canUndo()
            : undoStack->canRedo();
    }
    if (QAction* action = activeDocumentEditAction(undo))
    {
        if (action->isEnabled())
            return true;
    }

    QWidget* target = activeDocumentFocusTarget();
    if (auto* lineEdit = qobject_cast<QLineEdit*>(target))
    {
        return undo
            ? lineEdit->isUndoAvailable()
            : lineEdit->isRedoAvailable();
    }
    if (auto* plainTextEdit =
            qobject_cast<QPlainTextEdit*>(target))
    {
        return undo
            ? plainTextEdit->document()->isUndoAvailable()
            : plainTextEdit->document()->isRedoAvailable();
    }
    if (auto* textEdit = qobject_cast<QTextEdit*>(target))
    {
        return undo
            ? textEdit->document()->isUndoAvailable()
            : textEdit->document()->isRedoAvailable();
    }
    return false;
}

void MainWindow::executeActiveDocumentEditCommand(
    bool undo)
{
    if (QUndoStack* undoStack = activeDocumentUndoStack())
    {
        if (undo)
            undoStack->undo();
        else
            undoStack->redo();
        updateEditActionStates();
        return;
    }
    if (QAction* action = activeDocumentEditAction(undo))
    {
        if (action->isEnabled())
        {
            action->trigger();
            updateEditActionStates();
            return;
        }
    }

    QWidget* target = activeDocumentFocusTarget();
    if (auto* lineEdit = qobject_cast<QLineEdit*>(target))
    {
        if (undo)
            lineEdit->undo();
        else
            lineEdit->redo();
    }
    else if (auto* plainTextEdit =
                 qobject_cast<QPlainTextEdit*>(target))
    {
        if (undo)
            plainTextEdit->undo();
        else
            plainTextEdit->redo();
    }
    else if (auto* textEdit =
                 qobject_cast<QTextEdit*>(target))
    {
        if (undo)
            textEdit->undo();
        else
            textEdit->redo();
    }
    updateEditActionStates();
}

void MainWindow::updateEditActionStates()
{
    ui->undoAction->setEnabled(
        canExecuteActiveDocumentEditCommand(true));
    ui->redoAction->setEnabled(
        canExecuteActiveDocumentEditCommand(false));
}

void MainWindow::connectDocumentEditCommandState(
    QWidget* widget)
{
    const QList<QAction*> actions =
        widget->findChildren<QAction*>();
    for (QAction* action : actions)
    {
        connect(action, &QAction::changed,
            this, &MainWindow::updateEditActionStates);
    }

    const QList<QUndoStack*> undoStacks =
        widget->findChildren<QUndoStack*>();
    for (QUndoStack* undoStack : undoStacks)
    {
        connect(undoStack, &QUndoStack::canUndoChanged,
            this, &MainWindow::updateEditActionStates);
        connect(undoStack, &QUndoStack::canRedoChanged,
            this, &MainWindow::updateEditActionStates);
    }

    const QList<QLineEdit*> lineEdits =
        widget->findChildren<QLineEdit*>();
    for (QLineEdit* lineEdit : lineEdits)
    {
        connect(lineEdit, &QLineEdit::textChanged,
            this, &MainWindow::updateEditActionStates);
    }

    const QList<QPlainTextEdit*> plainTextEdits =
        widget->findChildren<QPlainTextEdit*>();
    for (QPlainTextEdit* textEdit : plainTextEdits)
    {
        connect(textEdit->document(), &QTextDocument::undoAvailable,
            this, &MainWindow::updateEditActionStates);
        connect(textEdit->document(), &QTextDocument::redoAvailable,
            this, &MainWindow::updateEditActionStates);
    }

    const QList<QTextEdit*> textEdits =
        widget->findChildren<QTextEdit*>();
    for (QTextEdit* textEdit : textEdits)
    {
        connect(textEdit->document(), &QTextDocument::undoAvailable,
            this, &MainWindow::updateEditActionStates);
        connect(textEdit->document(), &QTextDocument::redoAvailable,
            this, &MainWindow::updateEditActionStates);
    }
}

void MainWindow::onUndoCurrentDocument()
{
    executeActiveDocumentEditCommand(true);
}

void MainWindow::onRedoCurrentDocument()
{
    executeActiveDocumentEditCommand(false);
}

void MainWindow::onSubWindowActivated(QMdiSubWindow* window)
{
    if (window)
    {
        statusLabel->setText(window->windowTitle());
        QWidget* documentWidget = window->widget();
        if (!lastDocumentFocusWidget ||
            (lastDocumentFocusWidget != documentWidget &&
             !documentWidget->isAncestorOf(
                 lastDocumentFocusWidget)))
        {
            lastDocumentFocusWidget.clear();
        }
    }
    else
    {
        statusLabel->setText(tr("就绪 - 拖放文件到此处可直接打开"));
        lastDocumentFocusWidget.clear();
    }
    refreshPlaytestTargetPresentation();
    updateDesktopRunActionStates();
    updateEditActionStates();
    updateProjectDocumentSession();
}

void MainWindow::onCloseCurrentWindow()
{
    auto activeWindow = ui->mdiArea->activeSubWindow();
    if (activeWindow)
    {
        activeWindow->close();
    }
}

void MainWindow::onOpenRecentFile()
{
    auto action = qobject_cast<QAction*>(sender());
    if (action)
    {
        QString fileName = action->data().toString();
        if (!fileName.isEmpty())
        {
            openFileByType(fileName);
        }
    }
}

void MainWindow::onClearRecentFiles()
{
    recentFiles.clear();
    QSettings settings = createSettings();
    settings.setValue("recentFiles", recentFiles);
    updateRecentFileActions();
    if (ProjectManager::instance().isProjectOpen())
    {
        ProjectManager::instance().clearRecentFiles();
    }
}

void MainWindow::onDarkTheme()
{
    ThemeManager::instance().setTheme(ThemeManager::Theme::Dark);
}

void MainWindow::onLightTheme()
{
    ThemeManager::instance().setTheme(ThemeManager::Theme::Light);
}

void MainWindow::onThemeChanged(ThemeManager::Theme theme)
{
    ui->darkThemeAction->setChecked(theme == ThemeManager::Theme::Dark);
    ui->lightThemeAction->setChecked(theme == ThemeManager::Theme::Light);

    if (ProjectManager::instance().isProjectOpen())
    {
        QString themeName = (theme == ThemeManager::Theme::Dark) ? "dark" : "light";
        ProjectManager::instance().setTheme(themeName);
    }

    // QMdiArea caches its background brush instead of following later
    // application-palette changes. Refresh it explicitly so the light theme
    // does not retain the dark workspace background.
    ui->mdiArea->setBackground(
        QBrush(QApplication::palette().color(QPalette::Dark)));
    ui->mdiArea->update();
    const auto subWindows = ui->mdiArea->subWindowList();
    for (auto* subWindow : subWindows)
    {
        subWindow->setPalette(QApplication::palette());
        subWindow->update();
        if (subWindow->widget())
        {
            subWindow->widget()->setPalette(QApplication::palette());
            subWindow->widget()->update();
        }
    }
}

void MainWindow::onNewProject()
{
    if (projectAssetMigrationInProgress)
        return;

    QString filePath = QFileDialog::getSaveFileName(this,
        tr("新建项目"), QString(),
        tr("剑侠情缘项目文件 (*.jxqyproj);;所有文件 (*)"));

    if (filePath.isEmpty())
        return;

    if (!filePath.endsWith(".jxqyproj", Qt::CaseInsensitive))
        filePath += ".jxqyproj";

    const ProjectManager& projectManager = ProjectManager::instance();
    ProjectResourceConfiguration initialConfiguration;
    if (projectManager.isProjectOpen())
    {
        initialConfiguration.sourceAssetsRoot =
            projectManager.sourceAssetsRoot();
        initialConfiguration.editableAssetsRoot =
            projectManager.editableAssetsRoot();
        initialConfiguration.activeResourcePackId =
            projectManager.activeResourcePackId();
        initialConfiguration.activeResourcePackEntryKey =
            projectManager.activeResourcePackEntryKey();
    }
    else
    {
        initialConfiguration.editableAssetsRoot = resourceCollectionRoot();
    }

    ProjectSettingsDialog dialog(
        ProjectSettingsDialog::Mode::CreateProject,
        filePath, initialConfiguration, this);
    if (dialog.exec() == QDialog::Accepted)
        createProject(filePath, dialog.configuration());
}

bool MainWindow::createProject(const QString& filePath)
{
    if (projectAssetMigrationInProgress ||
        filePath.isEmpty())
        return false;

    return createProjectInternal(filePath,
        ProjectResourceConfiguration(), nullptr);
}

bool MainWindow::createProject(
    const QString& filePath,
    const ProjectResourceConfiguration& resourceConfiguration)
{
    if (projectAssetMigrationInProgress ||
        filePath.isEmpty())
        return false;

    ProjectResourceConfiguration resolvedConfiguration;
    ResourcePackSelection selection;
    if (!validateProjectResourceConfiguration(filePath,
            resourceConfiguration, resolvedConfiguration, selection))
    {
        return false;
    }

    resolvedConfiguration.activeResourcePackId =
        selection.activeResourcePackId;
    resolvedConfiguration.activeResourcePackEntryKey =
        selection.activeResourcePackEntryKey;
    return createProjectInternal(
        filePath, resolvedConfiguration, &selection);
}

bool MainWindow::createProjectInternal(
    const QString& filePath,
    const ProjectResourceConfiguration& resourceConfiguration,
    const ResourcePackSelection* selection)
{
    if (projectAssetMigrationInProgress)
        return false;

    const QString activeContentRoot = selection
        ? selection->activeRoot : QString();
    const QString collectionRoot = selection
        ? resourceConfiguration.editableAssetsRoot : QString();

    if (!recoverFileTransactions(activeContentRoot))
        return false;

    updateProjectDocumentSession();
    auto& projectManager = ProjectManager::instance();
    QList<PreparedAssetsPathSwitch> prepared;
    if (!prepareAssetsPathChange(
            activeContentRoot, collectionRoot, prepared))
        return false;

    const QString previousSettingsPath = createSettings().value("assetsPath").toString();
    if (!persistAssetsPathSetting(collectionRoot))
    {
        if (storyGraphWindow)
        {
            refreshStoryGraphAnalysis(false);
            synchronizeStoryGraphRuntimeTraceSession();
        }
        return false;
    }

    if (projectManager.newProject(filePath, resourceConfiguration))
    {
        commitAssetsPathChange(prepared);
        updateResourceProfileEditors();
        applyProjectSettings(false);
        QSettings settings = createSettings();
        settings.setValue("lastProject", filePath);
        settings.sync();
        statusLabel->setText(tr("项目已创建: %1").arg(filePath));
        return true;
    }
    else
    {
        persistAssetsPathSetting(previousSettingsPath);
        if (storyGraphWindow)
        {
            refreshStoryGraphAnalysis(false);
            synchronizeStoryGraphRuntimeTraceSession();
        }
        QMessageBox::warning(this, tr("创建失败"),
            tr("无法创建项目文件: %1").arg(filePath));
        return false;
    }
}

void MainWindow::onProjectSettings()
{
    if (projectAssetMigrationInProgress)
        return;

    const ProjectManager& projectManager = ProjectManager::instance();
    if (!projectManager.isProjectOpen())
        return;

    ProjectResourceConfiguration initialConfiguration;
    initialConfiguration.sourceAssetsRoot =
        projectManager.sourceAssetsRoot();
    initialConfiguration.editableAssetsRoot =
        projectManager.editableAssetsRoot();
    initialConfiguration.activeResourcePackId =
        projectManager.activeResourcePackId();
    initialConfiguration.activeResourcePackEntryKey =
        projectManager.activeResourcePackEntryKey();

    ProjectSettingsDialog dialog(
        ProjectSettingsDialog::Mode::EditProject,
        projectManager.projectFilePath(), initialConfiguration, this);
    if (dialog.exec() == QDialog::Accepted)
        applyProjectResourceConfiguration(dialog.configuration());
}

void MainWindow::onProjectRuntimeConfiguration()
{
    if (projectAssetMigrationInProgress)
        return;

    ProjectManager& projectManager = ProjectManager::instance();
    if (!projectManager.isProjectOpen())
        return;

    ProjectRuntimeConfigurationDialog dialog(
        projectManager.runtimeConfiguration(), activeAssetsPath(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    ProjectRuntimeConfigurationValidationResult validationResult;
    if (!projectManager.saveRuntimeConfiguration(
            dialog.configuration(), &validationResult))
    {
        if (validationResult.isValid())
        {
            QMessageBox::warning(
                this,
                tr("保存运行配置失败"),
                tr("无法写入项目文件，请检查文件或目录是否可写。"));
        }
        else
        {
            const QString sceneDescription =
                validationResult.sceneIndex >= 0
                ? tr("场景 %1").arg(validationResult.sceneIndex + 1)
                : tr("项目配置");
            QMessageBox::warning(
                this,
                tr("运行配置无效"),
                tr("%1 的字段“%2”未通过校验。")
                    .arg(sceneDescription, validationResult.fieldName));
        }
        return;
    }

    refreshDesktopRunProjectContext();
    statusLabel->setText(tr("项目运行配置已保存"));
}

bool MainWindow::applyProjectResourceConfiguration(
    const ProjectResourceConfiguration& resourceConfiguration)
{
    if (projectAssetMigrationInProgress)
        return false;

    auto& projectManager = ProjectManager::instance();
    if (!projectManager.isProjectOpen())
        return false;

    ProjectResourceConfiguration resolvedConfiguration;
    ResourcePackSelection selection;
    if (!validateProjectResourceConfiguration(
            projectManager.projectFilePath(), resourceConfiguration,
            resolvedConfiguration, selection) ||
        !recoverFileTransactions(selection.activeRoot))
    {
        return false;
    }
    resolvedConfiguration.activeResourcePackId =
        selection.activeResourcePackId;
    resolvedConfiguration.activeResourcePackEntryKey =
        selection.activeResourcePackEntryKey;

    QList<PreparedAssetsPathSwitch> prepared;
    if (!prepareAssetsPathChange(selection.activeRoot,
            resolvedConfiguration.editableAssetsRoot, prepared))
    {
        return false;
    }

    const QString previousSettingsPath =
        createSettings().value("assetsPath").toString();
    if (!persistAssetsPathSetting(
            resolvedConfiguration.editableAssetsRoot))
    {
        if (storyGraphWindow)
        {
            refreshStoryGraphAnalysis(false);
            synchronizeStoryGraphRuntimeTraceSession();
        }
        return false;
    }

    if (!projectManager.saveResourceConfiguration(resolvedConfiguration))
    {
        persistAssetsPathSetting(previousSettingsPath);
        if (storyGraphWindow)
        {
            refreshStoryGraphAnalysis(false);
            synchronizeStoryGraphRuntimeTraceSession();
        }
        QMessageBox::warning(this, tr("保存失败"),
            tr("无法保存项目资源设置，所有编辑器仍使用原目录。"));
        return false;
    }

    commitAssetsPathChange(prepared);
    updateResourceProfileEditors();
    refreshProjectWorkspace();
    statusLabel->setText(tr("项目资源设置已保存"));
    return true;
}

void MainWindow::onOpenProject()
{
    if (projectAssetMigrationInProgress)
        return;

    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("打开项目"),
        QString(),
        tr("剑侠情缘项目文件 (*.jxqyproj);;所有文件 (*)"),
        nullptr,
        QFileDialog::DontResolveSymlinks);

    if (filePath.isEmpty())
        return;

    openProject(filePath);
}

bool MainWindow::openProject(const QString& filePath)
{
    if (projectAssetMigrationInProgress ||
        filePath.isEmpty())
        return false;

    updateProjectDocumentSession();
    auto& projectManager = ProjectManager::instance();
    QString candidateAssetsPath;
    QString candidateResourcePackId;
    QString candidateResourcePackEntryKey;
    const bool reopeningCurrentProject =
        projectManager.isProjectOpen() &&
        EditorAssetPath::comparisonKey(
            projectManager.projectFilePath()) ==
        EditorAssetPath::comparisonKey(filePath);
    if (reopeningCurrentProject)
    {
        candidateAssetsPath =
            projectManager.editableAssetsRoot();
        candidateResourcePackId =
            projectManager.activeResourcePackId();
        candidateResourcePackEntryKey =
            projectManager.activeResourcePackEntryKey();
    }
    else if (!projectManager.readProjectResourceConfiguration(
                 filePath,
                 candidateAssetsPath,
                 candidateResourcePackId,
                 &candidateResourcePackEntryKey))
    {
        QMessageBox::warning(this, tr("打开失败"),
            tr("无法打开项目文件: %1").arg(filePath));
        return false;
    }

    ResourcePackSelection selection;
    if (!resolveProjectResourceContext(candidateAssetsPath,
            candidateResourcePackId,
            candidateResourcePackEntryKey,
            true,
            selection) ||
        !recoverFileTransactions(selection.activeRoot))
    {
        return false;
    }

    QList<PreparedAssetsPathSwitch> prepared;
    if (!prepareAssetsPathChange(selection.activeRoot,
            candidateAssetsPath, prepared))
        return false;

    const QString previousSettingsPath = createSettings().value("assetsPath").toString();
    if (!persistAssetsPathSetting(candidateAssetsPath))
    {
        if (storyGraphWindow)
        {
            refreshStoryGraphAnalysis(false);
            synchronizeStoryGraphRuntimeTraceSession();
        }
        return false;
    }

    if (projectManager.openProject(
            filePath,
            candidateAssetsPath,
            candidateResourcePackId,
            candidateResourcePackEntryKey))
    {
        commitAssetsPathChange(prepared);
        projectManager.setActiveResourcePackId(
            selection.activeResourcePackId);
        projectManager.setActiveResourcePackEntryKey(
            selection.activeResourcePackEntryKey);
        updateResourceProfileEditors();
        applyProjectSettings(false);
        QSettings settings = createSettings();
        settings.setValue("lastProject", filePath);
        settings.sync();
        if (lastProjectDocumentRestoreFailureCount == 0)
        {
            ui->statusBar->clearMessage();
            statusLabel->setText(tr("项目已打开: %1").arg(filePath));
        }
        return true;
    }
    else
    {
        persistAssetsPathSetting(previousSettingsPath);
        if (storyGraphWindow)
        {
            refreshStoryGraphAnalysis(false);
            synchronizeStoryGraphRuntimeTraceSession();
        }
        QMessageBox::warning(this, tr("打开失败"),
            tr("无法打开项目文件: %1").arg(filePath));
        return false;
    }
}

void MainWindow::onSaveProject()
{
    if (projectAssetMigrationInProgress)
        return;

    if (!ProjectManager::instance().isProjectOpen())
    {
        onNewProject();
        return;
    }

    auto& pm = ProjectManager::instance();
    pm.setEditableAssetsRoot(resourceCollectionRoot());
    pm.setTheme(ThemeManager::instance().currentThemeName());
    captureProjectWindowPlacement();
    pm.setWindowState(saveState());
    pm.setDocumentSession(captureProjectDocumentSession());
    pm.clearRecentFiles();
    for (auto iterator = recentFiles.crbegin(); iterator != recentFiles.crend(); ++iterator)
        pm.addRecentFile(*iterator);

    if (pm.saveProject())
    {
        QSettings settings = createSettings();
        settings.setValue("lastProject", pm.projectFilePath());
        statusLabel->setText(tr("项目已保存: %1").arg(pm.projectFilePath()));
    }
    else
    {
        QMessageBox::warning(this, tr("保存失败"),
            tr("无法保存项目文件。"));
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    bool stopDesktopRunOnAcceptedClose = false;
    if (desktopRunCoordinator &&
        desktopRunCoordinator->isActive())
    {
        const DesktopRunSessionCoordinatorState state =
            desktopRunCoordinator->state();
        const bool canRequestStop =
            state ==
                DesktopRunSessionCoordinatorState::Preparing ||
            state ==
                DesktopRunSessionCoordinatorState::Starting ||
            state ==
                DesktopRunSessionCoordinatorState::Running;
        const bool processIsStopping =
            state ==
                DesktopRunSessionCoordinatorState::Stopping;
        if ((canRequestStop || processIsStopping) &&
            QMessageBox::question(
                this,
                tr("试玩尚未结束"),
                canRequestStop
                ? tr("试玩仍在进行。是否停止试玩并关闭编辑器？")
                : tr("试玩正在停止。是否直接关闭编辑器？"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }

        if (canRequestStop)
            stopDesktopRunOnAcceptedClose = true;
    }

    QList<PreparedEditorClose> preparedEditors;
    if (!prepareEditorCloseTransaction(preparedEditors))
    {
        event->ignore();
        return;
    }

    auto& pm = ProjectManager::instance();
    bool abandonProjectAfterSaveFailure = false;
    if (pm.isProjectOpen())
    {
        pm.setEditableAssetsRoot(resourceCollectionRoot());
        pm.setTheme(ThemeManager::instance().currentThemeName());
        captureProjectWindowPlacement();
        pm.setWindowState(saveState());
        pm.setDocumentSession(captureProjectDocumentSession());
        if (!pm.saveProject())
        {
            if (QMessageBox::warning(this, tr("保存失败"),
                    tr("无法保存项目文件，关闭将导致未保存的项目设置丢失。是否仍要关闭？"),
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::No)
            {
                event->ignore();
                return;
            }
            abandonProjectAfterSaveFailure = true;
        }
    }

    closingApplication = true;
    commitEditorCloseTransaction(preparedEditors);

    for (QMdiSubWindow* subWindow : ui->mdiArea->subWindowList())
    {
        if (!subWindow->close())
        {
            closingApplication = false;
            event->ignore();
            return;
        }
    }

    if (pm.isProjectOpen() && !abandonProjectAfterSaveFailure)
    {
        pm.closeProject();
    }
    closingApplication = false;

    QSettings settings = createSettings();
    settings.setValue("windowGeometry", saveGeometry());
    settings.setValue("windowState", saveState());

    if (stopDesktopRunOnAcceptedClose)
        desktopRunCoordinator->requestStop();

    event->accept();
}

bool MainWindow::prepareEditorCloseTransaction(
    QList<PreparedEditorClose>& prepared)
{
    prepared.clear();
    for (QMdiSubWindow* subWindow : ui->mdiArea->subWindowList())
    {
        auto* participant =
            dynamic_cast<CloseTransactionParticipant*>(subWindow->widget());
        if (!participant)
            continue;

        ClosePlan plan = participant->prepareCloseTransaction();
        if (plan.isCancelled())
            return false;
        prepared.append({participant, plan});
    }

    for (const PreparedEditorClose& item : prepared)
    {
        if (!item.participant->resolveCloseTransaction(item.plan))
            return false;
    }
    return true;
}

void MainWindow::commitEditorCloseTransaction(
    const QList<PreparedEditorClose>& prepared)
{
    for (const PreparedEditorClose& item : prepared)
        item.participant->commitCloseTransaction(item.plan);
}

bool MainWindow::applyProjectSettings(bool applyAssetsPath)
{
    auto& pm = ProjectManager::instance();
    if (!pm.isProjectOpen())
        return false;

    restoringProjectDocumentSession = true;
    if (applyAssetsPath)
    {
        const QString projectAssetsPath = pm.editableAssetsRoot();
        if (!requestAssetsPathChange(projectAssetsPath))
        {
            restoringProjectDocumentSession = false;
            return false;
        }
    }

    recentFiles = pm.recentFiles();
    updateRecentFileActions();

    if (!pm.theme().isEmpty())
    {
        ThemeManager::Theme theme = (pm.theme() == "light") ? ThemeManager::Theme::Light : ThemeManager::Theme::Dark;
        ThemeManager::instance().setTheme(theme);
    }

    if (!pm.windowState().isEmpty())
    {
        restoreState(pm.windowState());
    }
    QTimer::singleShot(
        0,
        this,
        &MainWindow::compactDesktopRunDock);

    applyProjectWindowPlacement();

    updateWindowTitle();
    refreshProjectWorkspace();
    restoringProjectDocumentSession = false;
    lastProjectDocumentRestoreFailureCount =
        restoreProjectDocumentSession();
    return true;
}

void MainWindow::captureProjectWindowPlacement()
{
    auto& projectManager = ProjectManager::instance();
    QRect normalWindowGeometry = geometry();
    WindowDisplayMode mode = WindowDisplayMode::Normal;
    if (isFullScreen())
    {
        mode = WindowDisplayMode::FullScreen;
        if (normalGeometry().isValid())
            normalWindowGeometry = normalGeometry();
    }
    else if (isMaximized())
    {
        mode = WindowDisplayMode::Maximized;
        if (normalGeometry().isValid())
            normalWindowGeometry = normalGeometry();
    }
    projectManager.setWindowPlacement(normalWindowGeometry, mode);
}

void MainWindow::applyProjectWindowPlacement()
{
    auto& projectManager = ProjectManager::instance();
    if (!projectManager.hasWindowPlacement())
        return;

    QList<QRect> availableScreenGeometries;
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (primaryScreen)
        availableScreenGeometries.append(primaryScreen->availableGeometry());
    for (QScreen* screen : QGuiApplication::screens())
    {
        if (screen && screen != primaryScreen)
            availableScreenGeometries.append(screen->availableGeometry());
    }

    const QRect fallbackGeometry = geometry();
    const QSize safeMinimumSize = minimumSize().expandedTo(QSize(640, 480));
    const QRect safeGeometry = WindowPlacementPolicy::sanitizeNormalGeometry(
        projectManager.windowGeometry(), availableScreenGeometries,
        safeMinimumSize, fallbackGeometry);

    setWindowState(Qt::WindowNoState);
    if (safeGeometry.isValid())
        setGeometry(safeGeometry);

    switch (projectManager.windowMode())
    {
    case WindowDisplayMode::Maximized:
        setWindowState(Qt::WindowMaximized);
        break;
    case WindowDisplayMode::FullScreen:
        setWindowState(Qt::WindowFullScreen);
        break;
    case WindowDisplayMode::Normal:
    default:
        break;
    }

    projectManager.setWindowPlacement(safeGeometry, projectManager.windowMode());
}

QString MainWindow::activeAssetsPath() const
{
    const ProjectManager& projectManager = ProjectManager::instance();
    if (projectManager.isProjectOpen())
    {
        const ResourcePackSelection selection =
            ResourcePackScanner::resolveActivePack(
                projectManager.editableAssetsRoot(),
                projectManager.activeResourcePackId(),
                projectManager.activeResourcePackEntryKey());
        return selection.isReady() ? selection.activeRoot : QString();
    }
    return resourceCollectionRoot();
}

QString MainWindow::resourceCollectionRoot() const
{
    const ProjectManager& projectManager = ProjectManager::instance();
    if (projectManager.isProjectOpen())
        return projectManager.editableAssetsRoot();
    QSettings settings = createSettings();
    return settings.value("assetsPath").toString();
}

void MainWindow::updateWindowTitle()
{
    auto& pm = ProjectManager::instance();
    ui->projectSettingsAction->setEnabled(
        pm.isProjectOpen() && !projectAssetMigrationInProgress);
    ui->runtimeConfigurationAction->setEnabled(
        pm.isProjectOpen() && !projectAssetMigrationInProgress);
    ui->scriptProjectSearchAction->setEnabled(
        pm.isProjectOpen() && !projectAssetMigrationInProgress);
    if (pm.isProjectOpen())
    {
        QString projectName = QFileInfo(pm.projectFilePath()).fileName();
        setWindowTitle(tr("UPEdit-JXQY - %1").arg(projectName));
    }
    else
    {
        setWindowTitle(tr("UPEdit-JXQY"));
    }
    updateDesktopRunActionStates();
}
