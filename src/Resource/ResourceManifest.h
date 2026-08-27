#pragma once

#include <map>
#include <string>
#include <vector>
#include "ModReleaseMetadata.h"

enum class DefeatedNpcExperienceMode
{
	StoredExperience,
	LevelProductWithBonus
};

enum class LevelUpThresholdMode
{
	GreaterThanOrEqual,
	GreaterThan
};

enum class MagicEffectCalculationMode
{
	ReplaceAttack,
	AddToAttack
};

enum class ScriptNpcActionProfile
{
	Legacy,
	Yycs,
	Xjxqy
};

enum class ScriptNpcRuntimeProfile
{
	Legacy,
	Trilogy
};

enum class ScriptSpecialActionMode
{
	Replace,
	Overlay
};

enum class ScriptAddLifeMode
{
	PlayerRules,
	DirectClamp
};

// 资源清单字段，对应 game_profile.ini 的内容。
// 字段大小写不敏感（由 INIReader 处理）；空字段安全跳过。
struct ResourceManifest
{
	// 资源包根目录（归一化后以 '/' 结尾，或为空表示使用默认）。
	std::string resourceRoot;

	// [Game]
	std::string id;            // 资源包 ID，如 JXQY2/YYCS/XJXQY/自定义
	std::string name;          // 显示名称
	std::string author;        // 资源包署名；非空时由资源选择界面显示
	ModRelease::ModReleaseMetadata releaseMetadata;
	int type = 0;              // 对应 GAME_JXQY2/GAME_YYCS/GAME_XJXQY/GAME_CUSTOM
	bool useWav = false;       // 是否保留 wav 音乐优先逻辑

	bool typeDefined = false;

	// [Experience]
	// Each field is optional in a manifest. Runtime defaults are selected only
	// after the effective Game.Type has been resolved.
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
	// Resource-wide lower bound for one magic damage application. This is
	// deliberately independent from Game.Type.
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
	// Message supports {name} and {level}. Effect names are relative to
	// ini/magic; the random list is used when no sex-specific entry applies.
	std::string levelUpMessage = "{name}的等级得到提升！";
	std::vector<std::string> levelUpRandomEffects;
	std::string levelUpMaleEffect;
	std::string levelUpFemaleEffect;

	// [Resource]
	std::string dependencyId;   // 有序依赖资源包 Game.Id；多个 Id 以逗号分隔
	bool resourceOnly = false;  // 仅作为依赖加载，不能直接启动或显示在游戏选择列表
	bool textEncodingConverted = false; // 当前资源文本是否已统一转换为 UTF-8

	// [UI]
	// UI 基底与内容依赖分离；留空时为兼容旧配置，继续使用 [Resource] 依赖链。
	std::string uiBaseId;
	std::string uiProfile;      // 布局族标识，如 JXQY2/YYCS/XJXQY；不用于表达功能能力
	bool preferLocalUi = true;  // true: 当前资源包 UI 覆盖基底；false: UI 基底优先

	// [Features]
	// 独立的功能能力开关，键名不区分 ASCII 大小写。
	std::map<std::string, bool> features;

	// [Save]
	std::string saveNamespace;  // 移动端/Apple 平台用于隔离存档的命名空间

	// [Startup]
	std::vector<std::string> startupVideos;  // 文件名相对于 video 目录

	// [Title]
	std::string titleMenu;        // 标题菜单 INI
	std::string titleNewYearMenu; // 新年标题菜单 INI
	std::string titleMusic;       // 标题 BGM 文件名，相对于 music 目录
	std::string titleTeamVideo;   // 团队/制作组页面视频文件名，相对于 video 目录

	// [Team]
	std::string teamInfoFile;     // MOD 团队说明文本，相对于当前资源包根目录；不从依赖包回退

	// [NewGame]
	std::string newGameScript;    // 新游戏入口脚本

	// 从 game_profile.ini 文件加载。fileName 相对于资源根。
	bool loadFromFile(const std::string& relativePath);

	// 从内存缓冲加载。
	bool loadFromBuffer(const char* data, int len);

	// 创建与旧硬编码行为一致的默认 manifest。
	static ResourceManifest createDefault(const std::string& resourceRoot);

	// game_profile.ini 是已转换资源包的唯一标志；Game.Id 是唯一必填身份。
	bool isValid() const;

	// 基础游戏包必须没有内容依赖，并显式声明受支持的原版 Game.Type。
	bool isBaseGame() const;

	// 查询 [Features] 功能开关。未配置时返回 defaultValue。
	bool isFeatureEnabled(const std::string& featureName, bool defaultValue = false) const;

	// 按声明顺序解析 Resource.DependencyId，去除空项和不区分大小写的重复 Id。
	std::vector<std::string> getDependencyIds() const;

	DefeatedNpcExperienceMode resolvedDefeatedNpcExperienceMode() const;
	double resolvedExperienceMultiplier() const;
	LevelUpThresholdMode resolvedLevelUpThresholdMode() const;
	int resolvedPartnerFollowRadius() const;
	int resolvedPartnerFollowRunRadius() const;
	int resolvedMinimumMagicDamage() const;
	MagicEffectCalculationMode resolvedMagicEffectCalculationMode() const;
	ScriptNpcActionProfile resolvedNpcActionProfile() const;
	ScriptNpcRuntimeProfile resolvedNpcRuntimeProfile() const;
	ScriptSpecialActionMode resolvedSpecialActionMode() const;
	ScriptAddLifeMode resolvedAddLifeMode() const;
};
