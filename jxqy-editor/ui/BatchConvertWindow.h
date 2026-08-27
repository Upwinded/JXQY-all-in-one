#pragma once

#include <QPointer>
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>
#include "core/JxAssetMigrator.h"
#include "core/LuaScriptSyntaxValidator.h"

class QThread;
class QEvent;
class QCheckBox;

namespace Ui
{
class BatchConvertWindow;
}

// Extended migration result for GUI: adds Cancelled state
enum class GuiMigrationResult { Success, Partial, Failed, Cancelled };

struct ProjectAssetMigrationContext
{
    QString projectFilePath;
    QString sourceAssetsRoot;
    QString editableAssetsRoot;
    QString activeContentRoot;
    QString activeResourcePackId;
    QString activeResourcePackEntryKey;
    AssetMigrationOptions migrationOptions;
};

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
enum class BatchConvertWorkerKind
{
    Conversion,
    ScriptValidation
};

using BatchConvertWorkerTestHook =
    std::function<void(BatchConvertWorkerKind kind)>;

void setBatchConvertWorkerTestHookForTests(
    BatchConvertWorkerTestHook hook);
#endif

class BatchConvertWindow : public QWidget
{
    Q_OBJECT

public:
    using ProjectMigrationBeginCallback =
        std::function<bool(QString& errorMessage)>;
    using ProjectMigrationFinishCallback =
        std::function<bool(bool published, QString& errorMessage)>;
    using ProjectMigrationPolicyChangedCallback =
        std::function<void(const LegacyImageMigrationPolicy& policy)>;

    explicit BatchConvertWindow(QWidget* parent = nullptr);
    ~BatchConvertWindow();

    void setSourceDirectory(const QString& directory);
    bool configureProjectMigration(
        const ProjectAssetMigrationContext& context,
        ProjectMigrationPolicyChangedCallback policyChangedCallback,
        ProjectMigrationBeginCallback beginCallback,
        ProjectMigrationFinishCallback finishCallback);
    void clearProjectMigrationCallbacks();
    bool isProjectMigrationMode() const;
    bool isConversionInProgress() const;

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onSelectSourceDir();
    void onSelectOutputDir();
    void onStartConvert();
    void onCancelConvert();
    void onValidateScripts();

private:
    struct ConversionOutcome
    {
        GuiMigrationResult migrationResult = GuiMigrationResult::Failed;
        AssetMigrationReport migrationReport;
        bool cancelled = false;
    };

    struct UiCallbackState
    {
        BatchConvertWindow* window = nullptr;
    };

    struct ProjectMigrationCompletionState
    {
        bool windowAlive = true;
        bool activityStarted = false;
        QPointer<QObject> finishCallbackContext;
        ProjectMigrationFinishCallback finishCallback;
    };

    using LogCallback = std::function<void(const QString&)>;
    using ProgressCallback = std::function<void(int, int, const QString&)>;

    void appendLog(const QString& message);
    void setConvertingUI(bool converting);
    void applyProjectMigrationUi();
    void updateProjectMigrationContextText();
    void updateStartButtonState();
    bool finishProjectMigration(bool published, QString& errorMessage);
    void finishConversion(const QString& outputDir, const ConversionOutcome& outcome);
    void finishScriptValidation(const QString& assetsDir, const LuaScriptSyntaxReport& report);
    QString currentOutputDirectory() const;
    QCheckBox* legacyImageCategoryCheckBox(
        LegacyImageCategory category) const;
    LegacyImageMigrationPolicy legacyImagePolicyFromControls() const;
    void applyLegacyImagePolicyToControls(
        const LegacyImageMigrationPolicy& policy);
    QString legacyImageConversionSummary(
        const LegacyImageMigrationPolicy& policy) const;
    static GuiMigrationResult processAssetMigration(
        const QString& sourceDir,
        const QString& outputDir,
        const AssetMigrationOptions& options,
        AssetMigrationReport& report,
        const QString& legacyImageSummary,
        const std::shared_ptr<std::atomic_bool>& cancellationRequested,
        const LogCallback& logCallback,
        const ProgressCallback& progressCallback);
    void updateConversionOptions();
    void refreshMinimumMagicDamageDefault();

    Ui::BatchConvertWindow* ui;
    QThread* activeWorkerThread = nullptr;
    std::shared_ptr<std::atomic_bool>
        activeCancellationRequested;
    std::shared_ptr<UiCallbackState>
        activeUiCallbackState;
    std::shared_ptr<ProjectMigrationCompletionState>
        projectMigrationCompletionState =
            std::make_shared<
                ProjectMigrationCompletionState>();
    bool isConverting = false;
    bool minimumMagicDamageUserEdited = false;
    bool magicEffectCalculationModeUserEdited = false;
    QString lastReportPath;
    bool projectMigrationMode = false;
    bool projectMigrationActivityStarted = false;
    ProjectAssetMigrationContext projectMigrationContext;
    ProjectMigrationPolicyChangedCallback
        projectMigrationPolicyChangedCallback;
    ProjectMigrationBeginCallback projectMigrationBeginCallback;
};
