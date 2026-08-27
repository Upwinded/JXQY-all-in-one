#pragma once

#include <QWidget>
#include <QList>
#include <QSet>
#include <QString>
#include <functional>

#include "../core/GameProfile.h"
#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"

class QLineEdit;
class QByteArray;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QListWidget;
class QComboBox;
class QLabel;
class QPushButton;
class QListWidgetItem;
class QPlainTextEdit;
class QCloseEvent;
class QEvent;

// 资源清单编辑窗口：可视化编辑 game_profile.ini。
// 支持多资源包：扫描 assetsPath 下的资源包，列出后选择编辑。
// 若 game_profile.ini 不存在，提供"创建默认 Manifest"按钮。
class ResourceProfileEditorWindow : public QWidget,
                                    public AssetsPathSwitchParticipant,
                                    public CloseTransactionParticipant
{
    Q_OBJECT

public:
    explicit ResourceProfileEditorWindow(QWidget* parent = nullptr);
    ~ResourceProfileEditorWindow();

    // 设置 assets 根目录并扫描资源包。
    bool setAssetsBasePath(const QString& path);
    void setProjectResourceContext(
        const QString& activeResourcePackId,
        bool projectOpen,
        const QString& activeResourcePackEntryKey = QString());

    PathScope assetsPathScope() const override;
    Decision prepareAssetsPathSwitch(const QString& path) const override;
    bool resolveAssetsPathSwitch(Decision decision) override;
    void commitAssetsPathSwitch(const QString& path) override;
    QString currentAssetsPath() const override;

    ClosePlan prepareCloseTransaction() const override;
    bool resolveCloseTransaction(const ClosePlan& plan) override;
    void commitCloseTransaction(const ClosePlan& plan) override;

signals:
    void activateResourcePackRequested(
        const QString& resourcePackId,
        const QString& resourcePackEntryKey);
    void resourcePackIdentityChanged(
        const QString& previousId,
        const QString& currentId,
        const QString& resourcePackRoot);

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    virtual QString chooseManifestSavePath(
        const QString& suggestedPath);
    virtual QString chooseOnlinePackageSavePath(
        const QString& suggestedPath);
    virtual QString chooseProfileAssetFile(
        const QString& title,
        const QString& initialPath,
        const QString& filter);

private slots:
    // 资源包选择变化
    void onPackSelectionChanged(int index);
    void onSetActiveResourcePack();

    // Startup.Videos 列表操作
    void onAddVideo();
    void onRemoveVideo();
    void onMoveVideoUp();
    void onMoveVideoDown();

    // 文件选择按钮
    void onPickTitleMenu();
    void onPickTitleNewYearMenu();
    void onPickTitleMusic();
    void onPickTeamVideo();
    void onPickLevelUpMaleEffect();
    void onPickLevelUpFemaleEffect();
    void onPickReleaseCover();
    void onPickReleaseDescriptionFile();
    void onReloadReleaseDescriptionFile();
    void onPickTeamInfoFile();
    void onReloadTeamInfoFile();
    void onPickNewGameScript();

    // 保存
    void onSave();
    void onSaveAs();
    void onExportOnlinePackage();

    // 创建默认 Manifest
    void onCreateDefaultManifest();

    // 字段变化标记已修改
    void onFieldChanged();

private:
    // 路径模式：决定文件选择器生成的相对路径格式
    // - FileNameOnly: 只保存文件名（运行时会自动加 video\ 或 music\ 前缀）
    // - RelativeToPackRoot: 保存相对于资源包根目录的路径
    enum class PathMode
    {
        FileNameOnly,
        RelativeToPackRoot
    };

    void setupUi();
    void retranslateUi();
    void refreshPackList(bool showRecoveryError = true);
    void loadProfileToUi(const GameProfile& profile);
    GameProfile collectProfileFromUi() const;
    void updateVideoList();
    void markModified();
    bool confirmSaveIfModified();
    void updateProjectResourceContextUi();
    bool validateProfileForSave(const GameProfile& profile, const QString& packRoot);
    bool saveProfileTransaction(
        GameProfile& profile,
        const QString& manifestPath,
        const QString& packRoot,
        const ResourcePackInfo* indexedPack,
        QString& errorOrWarning);
    bool loadTeamInfoText(const QString& relativePath, bool showErrors);
    bool prepareTeamInfoWrite(GameProfile& profile, const QString& packRoot,
                              QString& resolvedPath, QByteArray& bytes,
                              bool& shouldWrite);
    bool loadReleaseDescriptionText(
        const QString& relativePath,
        bool showErrors);
    bool prepareReleaseDescriptionWrite(
        GameProfile& profile,
        const QString& packRoot,
        QString& resolvedPath,
        QByteArray& bytes,
        bool& shouldWrite);
    void updateReleaseCompatibilityLabel();
    void updateReleaseCoverPreview();
    void updateExperiencePreview();

    // 获取当前选中资源包的根目录
    QString currentPackRoot() const;

    // 为 QLineEdit 创建带"..."按钮的包装 widget，用于 QFormLayout
    QWidget* createPickerRow(
        QLineEdit* edit,
        std::function<void()> onPick,
        const QString& buttonObjectName = QString());

    // 为指定 QLineEdit 添加文件选择按钮的辅助
    void pickFileForEdit(QLineEdit* edit, const QString& defaultSubDir,
                         const QString& filter, PathMode mode,
                         bool requireSafeRelativeResourcePath = false);

    QString m_assetsBasePath;
    QList<ResourcePackInfo> m_packs;
    int m_currentPackIndex = -1;
    bool m_hasUnsavedChanges = false;
    bool m_updatingFromCode = false;

    // UI 控件
    QLabel* m_assetsPathLabel = nullptr;
    QComboBox* m_packCombo = nullptr;
    QLabel* m_packPathLabel = nullptr;
    QLabel* m_activePackLabel = nullptr;
    QPushButton* m_createDefaultBtn = nullptr;
    QPushButton* m_setActivePackBtn = nullptr;
    QString m_activeResourcePackId;
    QString m_activeResourcePackEntryKey;
    bool m_projectOpen = false;

    // [Game]
    QLineEdit* m_idEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_authorEdit = nullptr;
    QLineEdit* m_profileVersionEdit = nullptr;
    QSpinBox* m_typeSpin = nullptr;
    QCheckBox* m_inheritTypeCheck = nullptr;
    QCheckBox* m_useWavCheck = nullptr;

    // [Experience]
    QComboBox* m_experienceModeCombo = nullptr;
    QCheckBox* m_experienceMultiplierCheck = nullptr;
    QDoubleSpinBox* m_experienceMultiplierSpin = nullptr;
    QComboBox* m_levelUpThresholdModeCombo = nullptr;
    QLabel* m_experiencePreviewLabel = nullptr;

    // [Gameplay]
    QCheckBox* m_partnerFollowRadiusCheck = nullptr;
    QSpinBox* m_partnerFollowRadiusSpin = nullptr;
    QCheckBox* m_partnerFollowRunRadiusCheck = nullptr;
    QSpinBox* m_partnerFollowRunRadiusSpin = nullptr;

    // [Combat]
    QSpinBox* m_minimumMagicDamageSpin = nullptr;
    QComboBox* m_magicEffectCalculationModeCombo = nullptr;

    // [Script]
    QComboBox* m_npcActionProfileCombo = nullptr;
    QComboBox* m_npcRuntimeProfileCombo = nullptr;
    QComboBox* m_specialActionModeCombo = nullptr;
    QComboBox* m_addLifeModeCombo = nullptr;

    // [LevelUp]
    QLineEdit* m_levelUpMessageEdit = nullptr;
    QPlainTextEdit* m_levelUpRandomEffectsEdit = nullptr;
    QLineEdit* m_levelUpMaleEffectEdit = nullptr;
    QLineEdit* m_levelUpFemaleEffectEdit = nullptr;

    // [Release]
    QLineEdit* m_releaseDateEdit = nullptr;
    QLineEdit* m_minimumEngineVersionEdit = nullptr;
    QLabel* m_releaseCompatibilityLabel = nullptr;
    QLineEdit* m_releaseCoverEdit = nullptr;
    QLabel* m_releaseCoverPreviewLabel = nullptr;
    QLineEdit* m_releaseDescriptionFileEdit = nullptr;
    QPlainTextEdit* m_releaseDescriptionTextEdit = nullptr;
    QPushButton* m_reloadReleaseDescriptionBtn = nullptr;
    QPushButton* m_exportOnlinePackageBtn = nullptr;
    bool m_releaseDescriptionTextModified = false;
    QString m_loadedReleaseDescriptionFilePath;

    // [Resource] / [Save]
    QLineEdit* m_dependencyIdEdit = nullptr;
    QCheckBox* m_resourceOnlyCheck = nullptr;
    QCheckBox* m_textEncodingConvertedCheck = nullptr;
    QLineEdit* m_saveNamespaceEdit = nullptr;

    // [UI] / [Features]
    QLineEdit* m_uiBaseIdEdit = nullptr;
    QLineEdit* m_uiProfileEdit = nullptr;
    QCheckBox* m_preferLocalUiCheck = nullptr;
    QPlainTextEdit* m_featuresEdit = nullptr;

    // [Startup]
    QListWidget* m_videosList = nullptr;
    QPushButton* m_addVideoBtn = nullptr;
    QPushButton* m_removeVideoBtn = nullptr;
    QPushButton* m_videoUpBtn = nullptr;
    QPushButton* m_videoDownBtn = nullptr;

    // [Title]
    QLineEdit* m_titleMenuEdit = nullptr;
    QLineEdit* m_titleNewYearMenuEdit = nullptr;
    QLineEdit* m_titleMusicEdit = nullptr;
    QLineEdit* m_teamVideoEdit = nullptr;

    // [Team]
    QLineEdit* m_teamInfoFileEdit = nullptr;
    QPlainTextEdit* m_teamInfoTextEdit = nullptr;
    QPushButton* m_reloadTeamInfoBtn = nullptr;
    bool m_teamInfoTextModified = false;
    QString m_loadedTeamInfoFilePath;

    // Invalid UTF-8 read from a legacy manifest must not be silently replaced
    // with U+FFFD on save. Editing the corresponding field clears its entry.
    QSet<int> m_invalidLoadedReleaseFields;
    bool m_transactionRecoveryBlocked = false;
    QString m_transactionRecoveryErrorText;

    // [NewGame]
    QLineEdit* m_newGameScriptEdit = nullptr;

    // 操作
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_saveAsBtn = nullptr;
};
