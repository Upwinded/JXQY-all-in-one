#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QSet>
#include <memory>

#include "../../src/Resource/ModReleaseMetadata.h"
#include "../../src/Resource/ResourceManifest.h"

class QByteArray;

// 资源包 Manifest 数据，对应 game_profile.ini。
// 用于 jxqy-editor 可视化编辑资源清单。
struct GameProfile
{
    // 资源包根目录（绝对路径，用于显示和区分重复名称）
    QString rootPath;
    // game_profile.ini 完整路径
    QString manifestPath;

    // [Game]
    QString id;
    QString name;
    QString author;
    ModRelease::ModReleaseMetadata releaseMetadata;
    int type = 0;
    bool typeDefined = false;
    bool useWav = false;

    // [Experience]
    DefeatedNpcExperienceMode defeatedNpcExperienceMode =
        DefeatedNpcExperienceMode::StoredExperience;
    bool defeatedNpcExperienceModeDefined = false;
    double experienceMultiplier = 3.0;
    bool experienceMultiplierDefined = false;
    LevelUpThresholdMode levelUpThresholdMode =
        LevelUpThresholdMode::GreaterThanOrEqual;
    bool levelUpThresholdModeDefined = false;

    // [Gameplay]
    int partnerFollowRadius = 1;
    bool partnerFollowRadiusDefined = false;
    int partnerFollowRunRadius = 5;
    bool partnerFollowRunRadiusDefined = false;

    // [Combat]
    int minimumMagicDamage = 10;
    bool minimumMagicDamageDefined = false;
    MagicEffectCalculationMode magicEffectCalculationMode =
        MagicEffectCalculationMode::ReplaceAttack;
    bool magicEffectCalculationModeDefined = false;

    // [Script]
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

    // [LevelUp]
    QString levelUpMessage = QString::fromUtf8("{name}的等级得到提升！");
    QStringList levelUpRandomEffects;
    QString levelUpMaleEffect;
    QString levelUpFemaleEffect;

    // [Resource]
    QString dependencyId;
    bool resourceOnly = false;
    bool textEncodingConverted = false;

    // [UI]
    QString uiBaseId;
    QString uiProfile;
    bool preferLocalUi = true;

    // [Features]
    QMap<QString, bool> features;

    // [Save]
    QString saveNamespace;

    // [Startup]
    QStringList startupVideos;

	// [Title]
	QString titleMenu;
	QString titleNewYearMenu;
	QString titleMusic;
	QString titleTeamVideo;

    // [Team]
    QString teamInfoFile;

    // [NewGame]
    QString newGameScript;

    // 从 game_profile.ini 文件加载。
    bool loadFromFile(const QString& filePath);

    // 保存为 UTF-8 INI 文件。
    bool saveToFile(const QString& filePath) const;

    // 在不写盘的前提下合并已知字段并生成规范 UTF-8 字节。
    // 若现有源 manifest 存在但无法读取，则失败而不是重建并丢失未知内容。
    bool prepareSaveBytes(
        const QString& filePath,
        QByteArray& bytes,
        QByteArray* sourceBytes = nullptr,
        bool* sourceExists = nullptr) const;

    // 保存到当前 manifestPath。
    bool save() const;

    // 创建默认 manifest 数据（与运行时 ResourceManifest::createDefault 一致）。
    static GameProfile createDefault();

    // game_profile.ini 是已转换资源包的唯一标志；Game.Id 是唯一必填身份。
    bool isValid() const;

    // 将 startupVideos 列表转为逗号分隔字符串。
    QString videosToString() const;
};

// 资源包发现结果
struct ResourcePackInfo
{
    QString stableEntryKey; // Root/direct-child key; independent from Game.Id
    QString rootPath;       // 资源包根目录
    QString manifestPath;   // game_profile.ini 完整路径
    GameProfile profile;     // 已加载的 manifest
    QString effectiveSaveNamespace;
    bool saveNamespaceAdjusted = false;
    QStringList catalogDiagnostics;
};

enum class ResourcePackSelectionStatus
{
    Ready,
    SelectionRequired,
    ActivePackNotFound,
    ResourceIdConflict,
    InvalidAssetsRoot,
    RecoveryFailed
};

// 项目资源上下文持久化活动包 ID，并附带稳定目录条目键；
// 活动目录和 profile 每次从当前资源集合/game_profile.ini 解析，
// 避免目录、显示名或类型副本漂移。重复 Game.Id 会作为配置冲突拒绝。
struct ResourcePackSelection
{
    ResourcePackSelectionStatus status = ResourcePackSelectionStatus::Ready;
    QString collectionRoot;
    QString activeRoot;
    QString activeResourcePackId;
    QString activeResourcePackEntryKey;
    ResourcePackInfo activePack;
    QList<ResourcePackInfo> availablePacks;
    QStringList recoveryErrors;

    bool isReady() const
    {
        return status == ResourcePackSelectionStatus::Ready;
    }

    bool hasActivePack() const
    {
        return !activeResourcePackId.isEmpty();
    }
};

// 编辑器读取业务资源时使用的有效根。顺序与运行时内容资源根一致：
// 活动包、DependencyId 深度优先链、资源集合的 common。
struct ResourceContentRoot
{
    enum class Kind
    {
        Local,
        DependencyId,
        Common
    };

    QString rootPath;
    QString id;
    QString name;
    Kind kind = Kind::DependencyId;
    bool available = false;
};

struct ResourceContentRootResolution
{
    QString collectionRoot;
    QList<ResourcePackInfo> availablePacks;
    QList<ResourceContentRoot> roots;
    QStringList missingDependencyIds;
    QStringList missingPaths;
    QStringList catalogDiagnostics;
    QStringList recoveryErrors;
};

// 资源包扫描器：按运行时 game_profile.ini/resources.ini 契约发现可运行包。
class ResourcePackScanner
{
public:
    // 扫描指定目录下的资源包。
    // 若 assetsPath 本身含有效 game_profile.ini，则视为直接资源根。
    // 否则读取集合配置，并自动发现每个具有根级 game_profile.ini 的
    // 直接子目录；resources.ini 不登记或启用单个资源。
    static QList<ResourcePackInfo> scanPacks(const QString& assetsPath);
    static QList<ResourcePackInfo> scanPacks(
        const QString& assetsPath,
        QStringList& recoveryErrors);

    // 按显式稳定条目键优先、Game.Id 兜底解析项目活动包。条目键失效时，
    // 旧项目仍按 ID 选择目录顺序中的稳定首项；不会把 ID 当成条目键解释。
    // 无有效 profile 的目录保留为可编辑创作根；单包且两个选择字段都为空时自动选择。
    static ResourcePackSelection resolveActivePack(
        const QString& assetsPath,
        const QString& requestedPackId,
        const QString& requestedPackEntryKey = QString());

    // 判断指定目录是否含 game_profile.ini。
    static bool hasManifest(const QString& dirPath);

    // 获取目录下的 game_profile.ini 完整路径。
    static QString manifestPath(const QString& dirPath);

    // 解析内容资源读取根；不使用 UI.BaseId/PreferLocal，因为它们只属于 UI。
    // 无 manifest 的普通 assets 目录退化为单一本地根。
    static ResourceContentRootResolution resolveContentRoots(
        const QString& activeAssetsPath);
};
