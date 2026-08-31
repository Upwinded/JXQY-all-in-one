#pragma once

#include "AssetMigrationPolicy.h"
#include "../../src/Resource/ResourceManifest.h"

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QPair>
#include <functional>

enum class AssetResourceType
{
    All,
    Scripts,
    Maps,
    Images,
    Audio,
    Other
};

QString assetResourceTypeId(AssetResourceType type);
bool parseAssetResourceType(const QString& id, AssetResourceType& type);
QList<AssetResourceType> assetResourceDomainTypes();

struct AssetMigrationOptions
{
    bool convertScript = true;
    bool replaceWavWithMp3 = false;
    bool writeModProfile = true;
    QString modId;
    QString modName;
    // -1 表示不写 Game.Type，由内容依赖继承；0..3 为显式覆盖。
    int modType = -1;
    QString dependencyId = "JXQY2";
    QString saveNamespace;
    int minimumMagicDamage = 10;
    bool minimumMagicDamageDefined = false;
    MagicEffectCalculationMode magicEffectCalculationMode =
        MagicEffectCalculationMode::ReplaceAttack;
    bool magicEffectCalculationModeDefined = false;
    DefeatedNpcExperienceMode defeatedNpcExperienceMode =
        DefeatedNpcExperienceMode::StoredExperience;
    bool defeatedNpcExperienceModeDefined = false;
    double experienceMultiplier = 3.0;
    bool experienceMultiplierDefined = false;
    LevelUpThresholdMode levelUpThresholdMode =
        LevelUpThresholdMode::GreaterThanOrEqual;
    bool levelUpThresholdModeDefined = false;
    int partnerFollowRadius = 1;
    bool partnerFollowRadiusDefined = false;
    int partnerFollowRunRadius = 5;
    bool partnerFollowRunRadiusDefined = false;
    ScriptNpcActionProfile npcActionProfile =
        ScriptNpcActionProfile::Legacy;
    bool npcActionProfileDefined = false;
    ScriptNpcRuntimeProfile npcRuntimeProfile =
        ScriptNpcRuntimeProfile::Legacy;
    bool npcRuntimeProfileDefined = false;
    ScriptSpecialActionMode specialActionMode =
        ScriptSpecialActionMode::Replace;
    bool specialActionModeDefined = false;
    ScriptAddLifeMode addLifeMode =
        ScriptAddLifeMode::PlayerRules;
    bool addLifeModeDefined = false;
    QString titleMusic;
    bool titleMusicDefined = false;
    // Optional UI family override. When empty, migration infers the family
    // from the source UI images before falling back to Game.Type/Base.
    QString uiProfile;
    QString uiBaseId;
    bool preferLocalUi = true;
    QMap<QString, bool> features;
    // All performs a complete-project root replacement. One or more concrete
    // resource types publish only those domains as one transaction.
    QList<AssetResourceType> resourceTypes = {AssetResourceType::All};
    // Category defaults, conversion eligibility and crop eligibility come
    // from the single core policy used by GUI, CLI, migration and tests.
    LegacyImageMigrationPolicy legacyImages;
    QString includePrefix;
    // Legacy resource imports default to GBK/CP936. The caller owns the
    // source-encoding choice; migration does not guess between GBK and UTF-8.
    QString sourceEncoding = "gbk";
};

struct AssetResourceDomainReport
{
    bool selected = false;
    int processedFiles = 0;
    int writtenFiles = 0;
    int failedFiles = 0;
};

enum class AssetMigrationFileAction
{
    Copy,
    Convert,
    Skip,
    Fail
};

QString assetMigrationFileActionId(
    AssetMigrationFileAction action);
QStringList assetMigrationOutputPathCollisionSources(
    const QList<QPair<QString, QString>>&
        sourceOutputPaths);

struct AssetMigrationFileOutcome
{
    QString sourcePath;
    QString outputPath;
    QString outputSha256;
    QString domain;
    QString entryType = QStringLiteral("file");
    AssetMigrationFileAction action =
        AssetMigrationFileAction::Skip;
    QString reason;
    QString message;
    bool sourceScan = true;
};

struct AssetMigrationReport
{
    int processedFiles = 0;
    int writtenFiles = 0;
    int dependencyDuplicateFiles = 0;
    quint64 dependencyDuplicateBytes = 0;
    int warningCount = 0;
    int errorCount = 0;
    int scriptSyntaxTotalFiles = 0;
    int scriptSyntaxCheckedFiles = 0;
    int scriptSyntaxSkippedFiles = 0;
    int convertedAndCropped = 0;
    int convertedWithoutCrop = 0;
    int preservedImages = 0;
    int convertedMaps = 0;
    int preservedMapImages = 0;
    int skippedUnknownImages = 0;
    int failedImages = 0;
    bool cancelled = false;
    bool completeProject = false;
    QString reportFilePath;
    QString reportJsonFilePath;
    QStringList selectedResourceTypes;
    QMap<QString, AssetResourceDomainReport> resourceDomains;
    QMap<QString, QString> legacyImageModes;
    bool cropTransparentRequested = true;
    bool cropTransparentEffective = true;
    QStringList unsupportedScriptApis;
    QStringList unhandledScriptStatements;
    QStringList scriptSyntaxErrors;
    QStringList unavailableScripts;
    QMap<QString, QString> managedOutputSha256;
    QList<AssetMigrationFileOutcome> fileOutcomes;
    QString publishWarning;
    QString retainedBackupPath;
    QStringList logLines;
};

// Migration result status:
//   Success — migration completed, no errors recorded (exit 0)
//   Partial — some warnings but no errors (exit 1)
//   Failed  — errors occurred (exit 2)
enum class MigrationResult
{
    Success = 0,
    Partial = 1,
    Failed = 2
};

class JxAssetMigrator
{
public:
    using LogCallback = std::function<void(const QString&)>;
    using ProgressCallback = std::function<void(int current, int total, const QString& currentFile)>;
    using CancelCallback = std::function<bool()>;

    enum class FileSystemOperation
    {
        BackupRoot,
        PublishRoot,
        RestoreRoot,
        BackupEntry,
        PublishEntry,
        RollbackPublishedEntry,
        RestoreEntry,
        RemoveBackup,
        RemoveCreatedOutputRoot,
        PrepareModProfileOutput,
        CommitTextReport,
        CommitJsonReport
    };
#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
    using FileSystemFaultInjector = std::function<bool(
        FileSystemOperation operation,
        const QString& sourcePath,
        const QString& targetPath)>;
#endif

    // Resolve the conversion-time default from the first available content
    // dependency. No Game.Type inference is used.
    static int resolveMinimumMagicDamageDefault(
        const QString& outputDir,
        const AssetMigrationOptions& options);
    static MagicEffectCalculationMode resolveMagicEffectCalculationModeDefault(
        const QString& outputDir,
        const AssetMigrationOptions& options);

    MigrationResult migrate(const QString& sourceDir,
        const QString& outputDir,
        const AssetMigrationOptions& options,
        AssetMigrationReport& report,
        const LogCallback& logCallback = LogCallback(),
        const ProgressCallback& progressCallback = ProgressCallback(),
        const CancelCallback& cancelCallback = CancelCallback());

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
    static void setFileSystemFaultInjectorForTests(
        FileSystemFaultInjector injector);
#endif

private:
    QString normalizePath(QString path) const;
    QString mapOutputRelativePath(const QString& relativePath) const;

    bool copyFileReplacing(const QString& sourcePath, const QString& outputPath, AssetMigrationReport& report);
    bool writeTextFileUtf8(const QString& outputPath, const std::string& content, bool withBom, AssetMigrationReport& report);
    bool processTextFile(const QString& sourcePath, const QString& outputPath, const QString& relativePath,
        const AssetMigrationOptions& options, AssetMigrationReport& report);
    bool processMapFile(const QString& sourcePath, const QString& outputPath, const QString& relativePath,
        const AssetMigrationOptions& options, AssetMigrationReport& report);
    bool processRawCopyFile(const QString& sourcePath, const QString& outputPath, AssetMigrationReport& report);
    bool processRuntimeJpegFile(const QString& sourcePath,
        const QString& outputPath, const QString& relativePath,
        AssetMigrationReport& report);
    bool processImageFile(const QString& sourcePath, const QString& outputPath,
        const QString& relativePath, LegacyImageCategory category,
        const AssetMigrationOptions& options, AssetMigrationReport& report);

    std::string rewriteLegacyJxReferences(const std::string& content, const QString& relativePath) const;
    std::string rewriteMapNameIniToIdentity(const std::string& content) const;
    std::string normalizeObjectResourceIni(const std::string& content, const QString& relativePath) const;
    std::string applyUiDefaults(const std::string& content, const QString& relativePath,
        const AssetMigrationOptions& options) const;
    std::string replacePlayMusicWavWithMp3(const std::string& content) const;

    bool writeModProfileFile(const QString& outputDir, const AssetMigrationOptions& options,
        AssetMigrationReport& report);
    void ensureMoneyDropScripts(
        const QString& outputDir,
        AssetMigrationReport& report);
    void ensureKnownScriptLocations(
        const QString& outputDir,
        AssetMigrationReport& report);
    void ensureChooseMenuFiles(const QString& outputDir, const QString& uiBaseRoot,
        const AssetMigrationOptions& options, AssetMigrationReport& report);
    bool alignUiPresentationWithBase(const QString& outputDir, const QString& uiBaseRoot,
        AssetMigrationReport& report);
    void convertTalkDatToTalkIndex(const QString& inputDir, const QString& outputDir,
        const AssetMigrationOptions& options, AssetMigrationReport& report, const LogCallback& logCallback);
    void scanUnsupportedScriptApis(const std::string& convertedContent, const QString& sourcePath, AssetMigrationReport& report) const;
    bool writeReportFile(const QString& outputDir, AssetMigrationReport& report) const;
    static bool writeReportJsonFile(const QString& outputDir, AssetMigrationReport& report, MigrationResult status);
    static void appendReportLog(AssetMigrationReport& report, const LogCallback& logCallback, const QString& message);
};
