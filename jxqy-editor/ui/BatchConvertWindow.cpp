#include "BatchConvertWindow.h"
#include "ui_BatchConvertWindow.h"
#include "../core/JxAssetMigrator.h"
#include "../core/AuthoringMutationGate.h"
#include "../core/EditorProcessLifecycle.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QScrollBar>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QDesktopServices>
#include <QEvent>
#include <QMetaObject>
#include <QThread>
#include <QUrl>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace
{
#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
std::mutex& batchConvertWorkerTestHookMutex()
{
    static auto* mutex = new std::mutex;
    return *mutex;
}

BatchConvertWorkerTestHook& batchConvertWorkerTestHook()
{
    static auto* hook =
        new BatchConvertWorkerTestHook;
    return *hook;
}

void invokeBatchConvertWorkerTestHook(
    BatchConvertWorkerKind kind)
{
    BatchConvertWorkerTestHook hook;
    {
        const std::lock_guard<std::mutex> lock(
            batchConvertWorkerTestHookMutex());
        hook = batchConvertWorkerTestHook();
    }
    if (hook)
        hook(kind);
}
#endif

class MutationLeaseResetGuard
{
public:
    explicit MutationLeaseResetGuard(
        std::shared_ptr<std::optional<
            AuthoringMutationGate::Lease>> lease)
        : lease(std::move(lease))
    {
    }

    ~MutationLeaseResetGuard()
    {
        if (lease)
            lease->reset();
    }

private:
    std::shared_ptr<std::optional<
        AuthoringMutationGate::Lease>> lease;
};

QString pathWithCanonicalExistingAncestor(const QString& path)
{
    QString currentPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList missingParts;
    QFileInfo currentInfo(currentPath);
    while (!currentInfo.exists())
    {
        const QString fileName = currentInfo.fileName();
        const QString parentPath = QDir::cleanPath(currentInfo.absolutePath());
        if (fileName.isEmpty() || parentPath == currentPath)
            break;
        missingParts.prepend(fileName);
        currentPath = parentPath;
        currentInfo.setFile(currentPath);
    }

    QString resolvedPath = currentInfo.exists()
        ? currentInfo.canonicalFilePath()
        : currentPath;
    if (resolvedPath.isEmpty())
        resolvedPath = currentPath;
    for (const QString& part : missingParts)
        resolvedPath = QDir(resolvedPath).filePath(part);
    return QDir::cleanPath(resolvedPath);
}

}

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
void setBatchConvertWorkerTestHookForTests(
    BatchConvertWorkerTestHook hook)
{
    const std::lock_guard<std::mutex> lock(
        batchConvertWorkerTestHookMutex());
    batchConvertWorkerTestHook() =
        std::move(hook);
}
#endif

BatchConvertWindow::BatchConvertWindow(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::BatchConvertWindow)
{
    ui->setupUi(this);
    ui->sourceEncodingComboBox->setItemData(
        0, QStringLiteral("gbk"));
    ui->sourceEncodingComboBox->setItemData(
        1, QStringLiteral("utf8"));
    applyLegacyImagePolicyToControls(AssetMigrationOptions().legacyImages);

    connect(ui->sourceDirButton, &QPushButton::clicked, this, &BatchConvertWindow::onSelectSourceDir);
    connect(ui->outputDirButton, &QPushButton::clicked, this, &BatchConvertWindow::onSelectOutputDir);
    connect(ui->startButton, &QPushButton::clicked, this, &BatchConvertWindow::onStartConvert);
    connect(ui->cancelButton, &QPushButton::clicked, this, &BatchConvertWindow::onCancelConvert);
    connect(ui->validateScriptsButton, &QPushButton::clicked, this, &BatchConvertWindow::onValidateScripts);
    connect(ui->outputDirEdit, &QLineEdit::editingFinished,
        this, [this]() { refreshMinimumMagicDamageDefault(); });
    connect(ui->dependencyIdEdit, &QLineEdit::editingFinished,
        this, [this]() { refreshMinimumMagicDamageDefault(); });
    connect(ui->minimumMagicDamageSpinBox,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this, [this]() { minimumMagicDamageUserEdited = true; });
    connect(ui->magicEffectCalculationModeComboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this]() { magicEffectCalculationModeUserEdited = true; });
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        if (!item.allowsConversion)
            continue;
        if (QCheckBox* checkBox = legacyImageCategoryCheckBox(item.category))
        {
            connect(checkBox, &QCheckBox::toggled,
                this, &BatchConvertWindow::updateConversionOptions);
        }
    }
    connect(ui->cropTransparentCheckBox, &QCheckBox::toggled,
        this, &BatchConvertWindow::updateConversionOptions);
    connect(ui->confirmProjectOutputCheckBox, &QCheckBox::toggled,
        this, &BatchConvertWindow::updateStartButtonState);
    updateConversionOptions();
    applyProjectMigrationUi();
    refreshMinimumMagicDamageDefault();
    updateStartButtonState();
}

BatchConvertWindow::~BatchConvertWindow()
{
    const bool workerStillActive =
        activeWorkerThread != nullptr;
    if (activeCancellationRequested)
    {
        activeCancellationRequested->store(
            true,
            std::memory_order_release);
    }
    if (activeUiCallbackState)
        activeUiCallbackState->window = nullptr;
    if (projectMigrationCompletionState)
    {
        projectMigrationCompletionState->
            windowAlive = false;
    }
    if (activeWorkerThread)
    {
        disconnect(activeWorkerThread, nullptr, this, nullptr);
        activeWorkerThread = nullptr;
    }
    activeCancellationRequested.reset();
    activeUiCallbackState.reset();
    if (projectMigrationActivityStarted &&
        !workerStillActive)
    {
        QString ignoredError;
        finishProjectMigration(false, ignoredError);
    }
    delete ui;
}

void BatchConvertWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        setWindowTitle(tr("资源格式转换"));
        updateProjectMigrationContextText();
        setConvertingUI(isConverting);
    }
    QWidget::changeEvent(event);
}

void BatchConvertWindow::setSourceDirectory(const QString& directory)
{
    ui->sourceDirEdit->setText(directory);
}

bool BatchConvertWindow::configureProjectMigration(
    const ProjectAssetMigrationContext& context,
    ProjectMigrationPolicyChangedCallback policyChangedCallback,
    ProjectMigrationBeginCallback beginCallback,
    ProjectMigrationFinishCallback finishCallback)
{
    if (isConverting)
        return false;

    projectMigrationMode = true;
    projectMigrationContext = context;
    projectMigrationPolicyChangedCallback =
        ProjectMigrationPolicyChangedCallback();
    projectMigrationBeginCallback = std::move(beginCallback);
    projectMigrationCompletionState->
        finishCallback =
            std::move(finishCallback);
    QObject* finishCallbackContext = this;
    while (finishCallbackContext->parent())
    {
        finishCallbackContext =
            finishCallbackContext->parent();
    }
    projectMigrationCompletionState->
        finishCallbackContext =
            finishCallbackContext;
    projectMigrationCompletionState->
        activityStarted = false;
    projectMigrationCompletionState->
        windowAlive = true;
    projectMigrationActivityStarted = false;

    ui->sourceDirEdit->setText(context.sourceAssetsRoot);
    ui->outputDirEdit->setText(context.activeContentRoot);
    applyLegacyImagePolicyToControls(
        context.migrationOptions.legacyImages);
    ui->dependencyIdEdit->setText(
        context.migrationOptions.dependencyId);
    minimumMagicDamageUserEdited = false;
    magicEffectCalculationModeUserEdited = false;
    refreshMinimumMagicDamageDefault();
    const QString sourceEncoding =
        context.migrationOptions.sourceEncoding;
    const int sourceEncodingIndex =
        ui->sourceEncodingComboBox->findData(sourceEncoding);
    ui->sourceEncodingComboBox->setCurrentIndex(
        sourceEncodingIndex >= 0 ? sourceEncodingIndex : 0);
    ui->confirmProjectOutputCheckBox->setChecked(false);
    updateProjectMigrationContextText();
    updateConversionOptions();
    projectMigrationPolicyChangedCallback =
        std::move(policyChangedCallback);
    applyProjectMigrationUi();
    updateStartButtonState();
    return true;
}

bool BatchConvertWindow::isProjectMigrationMode() const
{
    return projectMigrationMode;
}

bool BatchConvertWindow::isConversionInProgress() const
{
    return isConverting ||
        activeWorkerThread != nullptr;
}

void BatchConvertWindow::clearProjectMigrationCallbacks()
{
    projectMigrationPolicyChangedCallback =
        ProjectMigrationPolicyChangedCallback();
    projectMigrationBeginCallback = ProjectMigrationBeginCallback();
    projectMigrationCompletionState->
        finishCallback =
            ProjectMigrationFinishCallback();
    projectMigrationCompletionState->
        finishCallbackContext.clear();
    projectMigrationCompletionState->
        activityStarted = false;
    projectMigrationActivityStarted = false;
}

void BatchConvertWindow::applyProjectMigrationUi()
{
    ui->projectContextGroup->setVisible(projectMigrationMode);
    ui->sourceDirEdit->setReadOnly(projectMigrationMode);
    ui->outputDirEdit->setReadOnly(projectMigrationMode);
    ui->sourceDirButton->setEnabled(!isConverting && !projectMigrationMode);
    ui->outputDirButton->setEnabled(!isConverting && !projectMigrationMode);
    ui->dependencyIdEdit->setEnabled(
        !isConverting && !projectMigrationMode);
    ui->confirmProjectOutputCheckBox->setEnabled(
        !isConverting && projectMigrationMode);
}

void BatchConvertWindow::updateProjectMigrationContextText()
{
    if (!projectMigrationMode)
        return;

    const QString resourcePackText =
        projectMigrationContext.activeResourcePackId.trimmed().isEmpty()
            ? tr("普通 loose 资源根（转换后重新解析 Game.Id）")
            : projectMigrationContext.activeResourcePackId;
    ui->projectContextLabel->setText(
        tr("项目：%1\n原始资源根（只读）：%2\n可编辑资源集合根：%3\n"
           "活动资源包：%4\n最终转换输出（活动内容根）：%5\n"
           "输出必须为空目录或既有转换输出；资源集合根不会被当作单包输出。")
            .arg(projectMigrationContext.projectFilePath,
                 projectMigrationContext.sourceAssetsRoot,
                 projectMigrationContext.editableAssetsRoot,
                 resourcePackText,
                 projectMigrationContext.activeContentRoot));
}

void BatchConvertWindow::updateStartButtonState()
{
    ui->startButton->setEnabled(
        !isConverting &&
        (!projectMigrationMode ||
         ui->confirmProjectOutputCheckBox->isChecked()));
}

bool BatchConvertWindow::finishProjectMigration(
    bool published, QString& errorMessage)
{
    if (!projectMigrationActivityStarted)
        return true;

    projectMigrationActivityStarted = false;
    projectMigrationCompletionState->
        activityStarted = false;
    if (projectMigrationCompletionState->
            finishCallbackContext.isNull() ||
        !projectMigrationCompletionState->
            finishCallback)
    {
        return true;
    }
    return projectMigrationCompletionState->
        finishCallback(
            published,
            errorMessage);
}

void BatchConvertWindow::appendLog(const QString& message)
{
    ui->logTextEdit->append(message);
    ui->logTextEdit->verticalScrollBar()->setValue(ui->logTextEdit->verticalScrollBar()->maximum());
}

void BatchConvertWindow::setConvertingUI(bool converting)
{
    ui->pathGroup->setEnabled(!converting);
    ui->optionsGroup->setEnabled(!converting);
    ui->validateScriptsButton->setEnabled(!converting);
    ui->cancelButton->setEnabled(converting);
    applyProjectMigrationUi();
    updateStartButtonState();
}

QString BatchConvertWindow::currentOutputDirectory() const
{
    QString outputDir = ui->outputDirEdit->text();
    if (outputDir.isEmpty())
    {
        QString sourceDir = ui->sourceDirEdit->text();
        if (sourceDir.isEmpty())
            return QString();

        while (sourceDir.endsWith("/") || sourceDir.endsWith("\\"))
            sourceDir.chop(1);
        outputDir = sourceDir + "_new";
    }

    return QDir::cleanPath(QFileInfo(outputDir).absoluteFilePath());
}

QCheckBox* BatchConvertWindow::legacyImageCategoryCheckBox(
    LegacyImageCategory category) const
{
    QString id = LegacyImageMigrationPolicy::definition(category).id;
    if (id.isEmpty())
        return nullptr;
    id[0] = id[0].toUpper();
    return findChild<QCheckBox*>(
        QStringLiteral("convert%1ImagesCheckBox").arg(id));
}

LegacyImageMigrationPolicy
BatchConvertWindow::legacyImagePolicyFromControls() const
{
    LegacyImageMigrationPolicy policy;
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        if (!item.allowsConversion)
            continue;
        QCheckBox* checkBox = legacyImageCategoryCheckBox(item.category);
        policy.setMode(item.category,
            checkBox && checkBox->isChecked()
                ? LegacyImageMode::Convert : LegacyImageMode::Preserve);
    }
    policy.setCropTransparent(
        ui->cropTransparentCheckBox->isChecked() &&
        policy.hasTransparentCropEligibleConversion());
    return policy;
}

void BatchConvertWindow::applyLegacyImagePolicyToControls(
    const LegacyImageMigrationPolicy& policy)
{
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        if (!item.allowsConversion)
            continue;
        if (QCheckBox* checkBox = legacyImageCategoryCheckBox(item.category))
        {
            const QSignalBlocker blocker(checkBox);
            checkBox->setChecked(
                policy.mode(item.category) == LegacyImageMode::Convert);
        }
    }
    const QSignalBlocker cropBlocker(ui->cropTransparentCheckBox);
    ui->cropTransparentCheckBox->setChecked(policy.cropTransparent());
}

QString BatchConvertWindow::legacyImageConversionSummary(
    const LegacyImageMigrationPolicy& policy) const
{
    QStringList categories;
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        if (!item.allowsConversion ||
            policy.mode(item.category) != LegacyImageMode::Convert)
        {
            continue;
        }
        if (QCheckBox* checkBox = legacyImageCategoryCheckBox(item.category))
        {
            categories.append(policy.shouldCrop(item.category)
                ? tr("%1（裁剪透明边缘）").arg(checkBox->text())
                : tr("%1（不裁剪）").arg(checkBox->text()));
        }
    }

    if (categories.isEmpty())
        return tr("所有旧图片类别均按原字节迁移；map/unknown 只迁移");
    return tr("转换为 IMP/IMG：%1；map/unknown 只迁移")
        .arg(categories.join(tr("、")));
}

void BatchConvertWindow::onSelectSourceDir()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("选择源目录"),
        QString(),
        QFileDialog::ShowDirsOnly |
            QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty())
    {
        ui->sourceDirEdit->setText(dir);
    }
}

void BatchConvertWindow::onSelectOutputDir()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("选择输出目录"),
        QString(),
        QFileDialog::ShowDirsOnly |
            QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty())
    {
        ui->outputDirEdit->setText(dir);
        refreshMinimumMagicDamageDefault();
    }
}

void BatchConvertWindow::onStartConvert()
{
    // Reentrancy guard
    if (isConverting)
        return;

    QString sourceDir = ui->sourceDirEdit->text();
    if (sourceDir.isEmpty())
    {
        QMessageBox::warning(this, tr("错误"),
            tr("请选择源目录！"));
        return;
    }

    if (!QDir(sourceDir).exists())
    {
        QMessageBox::warning(this, tr("错误"),
            tr("源目录不存在！"));
        return;
    }

    QString outputDir = ui->outputDirEdit->text();
    if (outputDir.isEmpty())
    {
        QString baseDir = sourceDir;
        while (baseDir.endsWith("/") || baseDir.endsWith("\\"))
            baseDir.chop(1);
        outputDir = baseDir + "_new";
    }

    // Normalize paths for comparison
    QString cleanSource = pathWithCanonicalExistingAncestor(sourceDir);
    QString cleanOutput = QDir::cleanPath(QFileInfo(outputDir).absoluteFilePath());
    QString comparisonOutput = pathWithCanonicalExistingAncestor(outputDir);
#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity pathCaseSensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity pathCaseSensitivity = Qt::CaseSensitive;
#endif

    if (projectMigrationMode &&
        (cleanSource.compare(pathWithCanonicalExistingAncestor(
             projectMigrationContext.sourceAssetsRoot),
             pathCaseSensitivity) != 0 ||
         comparisonOutput.compare(pathWithCanonicalExistingAncestor(
             projectMigrationContext.activeContentRoot),
             pathCaseSensitivity) != 0))
    {
        QMessageBox::critical(this, tr("项目导入配置已失效"),
            tr("项目导入的源目录或活动内容根已被改变。请关闭窗口并重新打开项目导入入口。"));
        return;
    }

    // Preflight: source = output is forbidden.
    if (cleanSource.compare(comparisonOutput, pathCaseSensitivity) == 0)
    {
        QMessageBox::critical(this, tr("错误"),
            tr("输出目录不能与源目录相同，否则会覆盖原始文件！"));
        return;
    }

    // Output inside source becomes input on repeated runs and creates nested copies.
    if (comparisonOutput.startsWith(cleanSource + "/", pathCaseSensitivity) ||
        comparisonOutput.startsWith(cleanSource + "\\", pathCaseSensitivity))
    {
        QMessageBox::critical(this, tr("错误"),
            tr("输出目录不能位于源目录内部，否则重复转换会递归复制已有输出。"));
        return;
    }
    QFileInfo outputInfo(cleanOutput);
    if ((outputInfo.exists() && (!outputInfo.isDir() || outputInfo.isSymLink())) ||
        QDir(comparisonOutput).isRoot())
    {
        QMessageBox::critical(this, tr("错误"),
            tr("输出路径必须是普通的非磁盘根目录。"));
        return;
    }

    if (!sourceDir.endsWith("/") && !sourceDir.endsWith("\\"))
        sourceDir += "/";
    if (!outputDir.endsWith("/") && !outputDir.endsWith("\\"))
        outputDir += "/";

    const QString migrationDependencyIds = ui->dependencyIdEdit->text().trimmed();

    AssetMigrationOptions migrationOptions = projectMigrationMode
        ? projectMigrationContext.migrationOptions
        : AssetMigrationOptions();
    migrationOptions.convertScript = ui->convertScriptCheckBox->isChecked();
    migrationOptions.replaceWavWithMp3 = ui->replaceWavCheckBox->isChecked();
    migrationOptions.legacyImages = legacyImagePolicyFromControls();
    migrationOptions.dependencyId = migrationDependencyIds;
    migrationOptions.minimumMagicDamage =
        ui->minimumMagicDamageSpinBox->value();
    if (minimumMagicDamageUserEdited)
    {
        migrationOptions.minimumMagicDamageDefined = true;
    }
    else if (!projectMigrationMode)
    {
        // Let the core converter preserve an explicit source profile value,
        // or resolve the default from the declared content dependency.
        migrationOptions.minimumMagicDamageDefined = false;
    }
    migrationOptions.magicEffectCalculationMode =
        ui->magicEffectCalculationModeComboBox->currentIndex() == 1
            ? MagicEffectCalculationMode::AddToAttack
            : MagicEffectCalculationMode::ReplaceAttack;
    if (magicEffectCalculationModeUserEdited)
    {
        migrationOptions.magicEffectCalculationModeDefined = true;
    }
    else if (!projectMigrationMode)
    {
        migrationOptions.magicEffectCalculationModeDefined = false;
    }
    const QString selectedSourceEncoding =
        ui->sourceEncodingComboBox->currentData().toString();
    migrationOptions.sourceEncoding = selectedSourceEncoding;

    // 仅继承类型（modType < 0）需要内容依赖来继承 Game.Type；显式 Type=0..3
    // （包括独立的 Type=3 MOD）允许 DependencyId 为空，与核心迁移器语义一致。
    const bool migrationDependencyRequired =
        migrationOptions.modType < 0;
    if (migrationDependencyRequired && migrationDependencyIds.isEmpty())
    {
        QMessageBox::critical(this, tr("错误"),
            tr("资源格式转换必须填写内容依赖 Game.Id（可用逗号分隔多个依赖）。"));
        return;
    }

    if (projectMigrationMode)
    {
        if (!ui->confirmProjectOutputCheckBox->isChecked())
            return;

        const auto answer = QMessageBox::warning(this,
            tr("确认项目资源导入/转换"),
            tr("即将只读扫描：\n%1\n\n并把完整转换结果原子发布到当前活动内容根：\n%2\n\n"
               "现有输出仅在它为空或属于既有转换结果时才会被替换。是否继续？")
                .arg(projectMigrationContext.sourceAssetsRoot,
                     projectMigrationContext.activeContentRoot),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;

        QString beginError;
        if (projectMigrationBeginCallback &&
            !projectMigrationBeginCallback(beginError))
        {
            QMessageBox::warning(this, tr("无法开始项目导入"),
                beginError.isEmpty()
                    ? tr("当前项目状态不允许开始资源导入。")
                    : beginError);
            return;
        }
        projectMigrationActivityStarted = true;
        projectMigrationCompletionState->
            activityStarted = true;
    }

    AuthoringMutationGate::Lease mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(outputDir);
    if (!mutationLease)
    {
        QString finishError;
        finishProjectMigration(false, finishError);
        QMessageBox::warning(
            this,
            tr("无法开始资源转换"),
            finishError.isEmpty()
            ? tr("资源目录正在进行其他写入；请稍后重试。")
            : tr("资源目录当前不可写，且项目导入状态恢复失败：%1")
                  .arg(finishError));
        return;
    }
    auto mutationLeaseHolder =
        std::make_shared<std::optional<
            AuthoringMutationGate::Lease>>();
    mutationLeaseHolder->emplace(
        std::move(mutationLease));
    auto cancellationRequested =
        std::make_shared<std::atomic_bool>(false);
    activeCancellationRequested =
        cancellationRequested;
    isConverting = true;
    setConvertingUI(true);
    ui->progressBar->setVisible(true);
    ui->progressBar->setValue(0);
    ui->logTextEdit->clear();

    appendLog(tr("=== 开始资源格式转换 ==="));
    appendLog(tr("源目录: %1").arg(sourceDir));
    appendLog(tr("输出目录: %1").arg(outputDir));

    auto outcome = std::make_shared<ConversionOutcome>();
    const QString legacyImageSummary =
        legacyImageConversionSummary(
            migrationOptions.legacyImages);
    auto uiCallbackState =
        std::make_shared<UiCallbackState>();
    uiCallbackState->window = this;
    activeUiCallbackState = uiCallbackState;
    QObject* const callbackDispatcher =
        QCoreApplication::instance();

    LogCallback logCallback =
        [uiCallbackState, callbackDispatcher](
            const QString& message)
    {
        if (callbackDispatcher == nullptr)
            return;
        QMetaObject::invokeMethod(
            callbackDispatcher,
            [uiCallbackState, message]()
            {
                if (uiCallbackState->window)
                {
                    uiCallbackState->window->
                        appendLog(message);
                }
            },
            Qt::QueuedConnection);
    };
    ProgressCallback progressCallback =
        [uiCallbackState, callbackDispatcher](
            int current,
            int total,
            const QString& currentFile)
    {
        if (callbackDispatcher == nullptr)
            return;
        QMetaObject::invokeMethod(
            callbackDispatcher,
            [uiCallbackState, current, total,
             currentFile]()
            {
                BatchConvertWindow* const window =
                    uiCallbackState->window;
                if (window == nullptr)
                    return;
                window->ui->progressBar->
                    setMaximum(total);
                window->ui->progressBar->
                    setValue(current);
                if (current % 100 == 0 ||
                    current == total)
                {
                    window->appendLog(
                        BatchConvertWindow::tr(
                            "%1/%2 %3")
                            .arg(current)
                            .arg(total)
                            .arg(currentFile));
                }
            },
            Qt::QueuedConnection);
    };

    QThread* workerThread = QThread::create(
        [sourceDir, outputDir, migrationOptions,
         legacyImageSummary, outcome,
         cancellationRequested,
         mutationLeaseHolder, logCallback,
         progressCallback]()
        {
            const MutationLeaseResetGuard
                releaseMutationLease(
                    mutationLeaseHolder);
#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
            invokeBatchConvertWorkerTestHook(
                BatchConvertWorkerKind::Conversion);
#endif
            outcome->migrationResult =
                BatchConvertWindow::processAssetMigration(
                    sourceDir,
                    outputDir,
                    migrationOptions,
                    outcome->migrationReport,
                    legacyImageSummary,
                    cancellationRequested,
                    logCallback,
                    progressCallback);
            outcome->cancelled =
                outcome->migrationResult == GuiMigrationResult::Cancelled;
        });
    registerEditorBackgroundWorker(
        workerThread);
    registerEditorExitProtectedWriteWorker(
        workerThread,
        [cancellationRequested]()
        {
            cancellationRequested->store(
                true,
                std::memory_order_release);
        });
    activeWorkerThread = workerThread;

    if (callbackDispatcher)
    {
        const std::shared_ptr<
            ProjectMigrationCompletionState>
            completionState =
                projectMigrationCompletionState;
        connect(
            workerThread,
            &QThread::finished,
            callbackDispatcher,
            [completionState, outcome]()
            {
                if (completionState->
                        windowAlive ||
                    !completionState->
                        activityStarted)
                {
                    return;
                }

                completionState->
                    activityStarted = false;
                if (!completionState->
                        finishCallback ||
                    completionState->
                        finishCallbackContext.isNull())
                {
                    return;
                }
                const bool published =
                    !outcome->cancelled &&
                    outcome->migrationResult !=
                        GuiMigrationResult::Failed;
                QString ignoredError;
                completionState->
                    finishCallback(
                        published,
                        ignoredError);
            },
            Qt::QueuedConnection);
    }
    connect(workerThread, &QThread::finished, this,
        [this, workerThread, outputDir, outcome]() {
            if (activeWorkerThread != workerThread)
                return;

            activeWorkerThread = nullptr;
            activeCancellationRequested.reset();
            if (activeUiCallbackState)
                activeUiCallbackState->window = nullptr;
            activeUiCallbackState.reset();
            finishConversion(outputDir, *outcome);
        });
    connect(
        workerThread,
        &QThread::finished,
        workerThread,
        &QObject::deleteLater);
    workerThread->start();
}

void BatchConvertWindow::finishConversion(
    const QString& outputDir,
    const ConversionOutcome& outcome)
{
    isConverting = false;
    setConvertingUI(false);
    lastReportPath = outcome.migrationReport.reportFilePath;

    const bool migrationPublished = !outcome.cancelled &&
        outcome.migrationResult != GuiMigrationResult::Failed;
    QString projectFinishError;
    const bool projectFinishOk = finishProjectMigration(
        migrationPublished, projectFinishError);
    QString projectFinishSuffix;
    if (migrationPublished && !projectFinishOk)
    {
        projectFinishSuffix = tr(
            "\n\n转换输出已经完整发布，但项目上下文未切换：%1")
                .arg(projectFinishError.isEmpty()
                    ? tr("请重新打开项目设置并选择该输出。")
                    : projectFinishError);
        appendLog(tr("[项目] 输出已发布，但项目上下文更新失败：%1")
            .arg(projectFinishError));
    }

    if (outcome.cancelled)
    {
        appendLog(tr("=== 转换已取消 ==="));
        QMessageBox::warning(this, tr("提示"),
            tr("转换已被用户取消！"));
    }
    else if (outcome.migrationResult == GuiMigrationResult::Failed)
    {
        appendLog(tr("=== 转换失败 ==="));
        ui->progressBar->setValue(100);
        const QString msg = tr("转换失败，存在错误，详见报告。");

        QMessageBox msgBox(this);
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowTitle(tr("提示"));
        msgBox.setText(msg);
        QPushButton* openDirBtn = msgBox.addButton(tr("打开输出目录"), QMessageBox::ActionRole);
        QPushButton* openReportBtn = nullptr;
        if (!lastReportPath.isEmpty())
            openReportBtn = msgBox.addButton(tr("打开报告"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();
        if (msgBox.clickedButton() == openDirBtn)
            QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir));
        else if (msgBox.clickedButton() == openReportBtn && !lastReportPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(lastReportPath));
    }
    else if (outcome.migrationResult == GuiMigrationResult::Partial)
    {
        appendLog(tr("=== 转换完成但存在警告 ==="));
        ui->progressBar->setValue(100);
        QString msg = tr("转换完成，但存在警告，详见报告。");
        msg += projectFinishSuffix;

        QMessageBox msgBox(this);
        msgBox.setIcon(projectFinishSuffix.isEmpty()
            ? QMessageBox::Information : QMessageBox::Warning);
        msgBox.setWindowTitle(tr("提示"));
        msgBox.setText(msg);
        QPushButton* openDirBtn = msgBox.addButton(tr("打开输出目录"), QMessageBox::ActionRole);
        QPushButton* openReportBtn = nullptr;
        if (!lastReportPath.isEmpty())
            openReportBtn = msgBox.addButton(tr("打开报告"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();
        if (msgBox.clickedButton() == openDirBtn)
            QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir));
        else if (msgBox.clickedButton() == openReportBtn && !lastReportPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(lastReportPath));
    }
    else
    {
        appendLog(tr("=== 转换完成 ==="));
        ui->progressBar->setValue(100);

        QMessageBox msgBox(this);
        msgBox.setIcon(projectFinishSuffix.isEmpty()
            ? QMessageBox::Information : QMessageBox::Warning);
        msgBox.setWindowTitle(tr("提示"));
        msgBox.setText(tr("资源格式转换完成！共处理 %1 个文件。").arg(
            outcome.migrationReport.processedFiles) +
            projectFinishSuffix);
        QPushButton* openDirBtn = msgBox.addButton(tr("打开输出目录"), QMessageBox::ActionRole);
        QPushButton* openReportBtn = nullptr;
        if (!lastReportPath.isEmpty())
            openReportBtn = msgBox.addButton(tr("打开报告"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();
        if (msgBox.clickedButton() == openDirBtn)
            QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir));
        else if (msgBox.clickedButton() == openReportBtn && !lastReportPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(lastReportPath));
    }
}

void BatchConvertWindow::onValidateScripts()
{
    if (isConverting)
        return;

    QString assetsDir = currentOutputDirectory();
    if (assetsDir.isEmpty())
    {
        QMessageBox::warning(this, tr("错误"),
            tr("请先选择输出目录，或设置源目录以推导默认输出目录。"));
        return;
    }

    if (!QDir(assetsDir).exists())
    {
        QMessageBox::warning(this, tr("错误"),
            tr("输出目录不存在，无法检查转换后脚本：%1").arg(assetsDir));
        return;
    }

    auto cancellationRequested =
        std::make_shared<std::atomic_bool>(false);
    activeCancellationRequested =
        cancellationRequested;
    isConverting = true;
    setConvertingUI(true);
    ui->progressBar->setVisible(true);
    ui->progressBar->setValue(0);
    ui->logTextEdit->clear();

    appendLog(tr("=== 开始检查转换后脚本 ==="));
    appendLog(tr("资源目录: %1").arg(assetsDir));

    auto report = std::make_shared<LuaScriptSyntaxReport>();
    auto uiCallbackState =
        std::make_shared<UiCallbackState>();
    uiCallbackState->window = this;
    activeUiCallbackState = uiCallbackState;
    QObject* const callbackDispatcher =
        QCoreApplication::instance();
    LuaScriptSyntaxValidator::ProgressCallback progressCallback =
        [uiCallbackState, callbackDispatcher](
            int current,
            int total,
            const QString& currentFile)
        {
            if (callbackDispatcher == nullptr)
                return;
            QMetaObject::invokeMethod(
                callbackDispatcher,
                [uiCallbackState, current, total,
                 currentFile]()
                {
                    BatchConvertWindow* const window =
                        uiCallbackState->window;
                    if (window == nullptr)
                        return;
                    window->ui->progressBar->
                        setMaximum(total);
                    window->ui->progressBar->
                        setValue(current);
                    if (current % 100 == 0 ||
                        current == total)
                    {
                        window->appendLog(
                            BatchConvertWindow::tr(
                                "%1/%2 %3")
                                .arg(current)
                                .arg(total)
                                .arg(currentFile));
                    }
                },
                Qt::QueuedConnection);
        };

    QThread* workerThread = QThread::create(
        [assetsDir, report, progressCallback,
         cancellationRequested]()
        {
#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
            invokeBatchConvertWorkerTestHook(
                BatchConvertWorkerKind::ScriptValidation);
#endif
            *report = LuaScriptSyntaxValidator::validateAssetsScripts(
                assetsDir,
                progressCallback,
                [cancellationRequested]() -> bool
                {
                    return cancellationRequested->load(
                        std::memory_order_acquire);
                });
        });
    registerEditorBackgroundWorker(
        workerThread);
    activeWorkerThread = workerThread;

    connect(workerThread, &QThread::finished, this,
        [this, workerThread, assetsDir, report]() {
            if (activeWorkerThread != workerThread)
                return;

            activeWorkerThread = nullptr;
            activeCancellationRequested.reset();
            if (activeUiCallbackState)
                activeUiCallbackState->window = nullptr;
            activeUiCallbackState.reset();
            finishScriptValidation(assetsDir, *report);
        });
    connect(
        workerThread,
        &QThread::finished,
        workerThread,
        &QObject::deleteLater);
    workerThread->start();
}

void BatchConvertWindow::finishScriptValidation(const QString& assetsDir, const LuaScriptSyntaxReport& report)
{
    isConverting = false;
    setConvertingUI(false);

    if (report.cancelled)
    {
        appendLog(tr("=== 脚本检查已取消 ==="));
        QMessageBox::warning(this, tr("提示"),
            tr("脚本检查已被用户取消。"));
        return;
    }

    if (report.scriptRootMissing)
    {
        appendLog(tr("未找到 script 子目录: %1").arg(QDir(assetsDir).filePath("script")));
        QMessageBox::warning(this, tr("提示"),
            tr("未找到 script 子目录，无法检查转换后脚本。"));
        return;
    }

    appendLog(tr("检查完成: 检查 %1 个，跳过 %2 个，错误 %3 个")
        .arg(report.checkedFiles)
        .arg(report.skippedFiles)
        .arg(report.failedFiles));

    for (const LuaScriptSyntaxIssue& issue : report.issues)
        appendLog(tr("错误: %1").arg(issue.toString()));

    ui->progressBar->setValue(ui->progressBar->maximum());

    if (report.hasErrors())
    {
        QMessageBox::warning(this, tr("脚本检查失败"),
            tr("转换后脚本存在 %1 个语法错误，详情见日志。")
                .arg(report.failedFiles));
    }
    else
    {
        QMessageBox::information(this, tr("脚本检查通过"),
            tr("转换后脚本语法检查通过。共检查 %1 个脚本，跳过 %2 个数据/备注文件。")
                .arg(report.checkedFiles)
                .arg(report.skippedFiles));
    }
}

void BatchConvertWindow::onCancelConvert()
{
    const std::shared_ptr<std::atomic_bool>
        cancellationRequested =
            activeCancellationRequested;
    if (!isConverting ||
        !cancellationRequested ||
        cancellationRequested->exchange(
            true,
            std::memory_order_acq_rel))
    {
        return;
    }

    ui->cancelButton->setEnabled(false);
    appendLog(tr(">>> 用户取消转换 <<<"));
}

void BatchConvertWindow::updateConversionOptions()
{
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        if (!item.allowsConversion)
            continue;
        if (QCheckBox* checkBox = legacyImageCategoryCheckBox(item.category))
        {
            checkBox->setEnabled(!isConverting);
        }
    }
    const LegacyImageMigrationPolicy policy =
        legacyImagePolicyFromControls();
    ui->cropTransparentCheckBox->setEnabled(
        !isConverting &&
        policy.hasTransparentCropEligibleConversion());
    ui->minimumMagicDamageSpinBox->setEnabled(
        !isConverting);
    ui->magicEffectCalculationModeComboBox->setEnabled(
        !isConverting);

    if (projectMigrationMode && projectMigrationPolicyChangedCallback)
        projectMigrationPolicyChangedCallback(policy);
    appendLog(tr("[资源格式转换] %1。")
        .arg(legacyImageConversionSummary(policy)));
    applyProjectMigrationUi();
    updateStartButtonState();
    setWindowTitle(tr("资源格式转换"));
}

void BatchConvertWindow::refreshMinimumMagicDamageDefault()
{
    if ((minimumMagicDamageUserEdited &&
         magicEffectCalculationModeUserEdited) ||
        ui->minimumMagicDamageSpinBox == nullptr ||
        ui->magicEffectCalculationModeComboBox == nullptr)
    {
        return;
    }

    AssetMigrationOptions options = projectMigrationMode
        ? projectMigrationContext.migrationOptions
        : AssetMigrationOptions();
    options.dependencyId = ui->dependencyIdEdit->text().trimmed();
    if (!minimumMagicDamageUserEdited)
    {
        const int minimumMagicDamage =
            JxAssetMigrator::resolveMinimumMagicDamageDefault(
                ui->outputDirEdit->text().trimmed(), options);
        const QSignalBlocker blocker(ui->minimumMagicDamageSpinBox);
        ui->minimumMagicDamageSpinBox->setValue(minimumMagicDamage);
    }
    if (!magicEffectCalculationModeUserEdited &&
        ui->magicEffectCalculationModeComboBox != nullptr)
    {
        const MagicEffectCalculationMode mode =
            JxAssetMigrator::resolveMagicEffectCalculationModeDefault(
                ui->outputDirEdit->text().trimmed(), options);
        const QSignalBlocker modeBlocker(
            ui->magicEffectCalculationModeComboBox);
        ui->magicEffectCalculationModeComboBox->setCurrentIndex(
            mode == MagicEffectCalculationMode::AddToAttack ? 1 : 0);
    }
}

GuiMigrationResult BatchConvertWindow::processAssetMigration(
    const QString& sourceDir,
    const QString& outputDir,
    const AssetMigrationOptions& options,
    AssetMigrationReport& report,
    const QString& legacyImageSummary,
    const std::shared_ptr<std::atomic_bool>&
        cancellationRequested,
    const LogCallback& logCallback,
    const ProgressCallback& progressCallback)
{
    JxAssetMigrator migrator;

    logCallback(tr("[资源格式转换] %1")
        .arg(legacyImageSummary));

    MigrationResult result = migrator.migrate(sourceDir, outputDir, options, report,
        [&logCallback](const QString& message) {
            logCallback(tr("[资源格式转换] %1").arg(message));
        },
        [&progressCallback](int current, int total, const QString& currentFile) {
            progressCallback(current, total, tr("[资源格式转换] %1").arg(currentFile));
        },
        [cancellationRequested]() -> bool
        {
            return cancellationRequested->load(
                std::memory_order_acquire);
        });

    if (!report.unsupportedScriptApis.isEmpty())
    {
        logCallback(tr("[资源格式转换] 脚本转换中仍有未处理的 API 调用: %1 个，详见报告")
            .arg(report.unsupportedScriptApis.size()));
    }
    if (!report.scriptSyntaxErrors.isEmpty())
    {
        logCallback(tr("[资源格式转换] 转换后脚本语法错误: %1 个，详见报告")
            .arg(report.scriptSyntaxErrors.size()));
    }
    if (!report.reportFilePath.isEmpty())
        logCallback(tr("[资源格式转换] 报告文件: %1").arg(report.reportFilePath));
    else
        logCallback(tr("[资源格式转换] 报告文件: 写入失败"));
    if (!report.reportJsonFilePath.isEmpty())
        logCallback(tr("[资源格式转换] JSON 报告: %1").arg(report.reportJsonFilePath));
    else
        logCallback(tr("[资源格式转换] JSON 报告: 写入失败"));

    if (report.cancelled)
        return GuiMigrationResult::Cancelled;
    switch (result)
    {
    case MigrationResult::Success: return GuiMigrationResult::Success;
    case MigrationResult::Partial: return GuiMigrationResult::Partial;
    case MigrationResult::Failed: return GuiMigrationResult::Failed;
    }
    return GuiMigrationResult::Failed;
}
