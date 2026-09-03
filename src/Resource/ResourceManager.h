#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "ResourceCatalog.h"

// 资源管理器：负责扫描资源包、读取 manifest、提供 active resource root。
// 在 Game::run() 加载 font/cursor 之前调用 initialize() 完成发现/选择。
class ResourceManager
{
public:
	enum class ResourceRemovalSavePolicy
	{
		Unselected,
		Preserve,
		Delete
	};

	enum class ResourceRemovalStatus
	{
		Success,
		InvalidSelection,
		NotRemovable,
		DependencyBlocked,
		TargetChanged,
		DeleteFailed
	};

	struct ResourceRemovalEntry
	{
		std::string gameId;
		std::string name;
		std::string version;
		std::string rootPath;
		std::string saveNamespace;
		std::uint64_t saveBytes = 0;
		bool saveExists = false;
		bool saveRemovable = true;
	};

	struct ResourceRemovalPlan
	{
		ResourceRemovalStatus status =
			ResourceRemovalStatus::InvalidSelection;
		std::vector<ResourceRemovalEntry> entries;
		std::vector<std::string> blockingResourceNames;
	};

	struct ResourceRemovalResult
	{
		ResourceRemovalStatus status =
			ResourceRemovalStatus::InvalidSelection;
		std::string failedPath;
		std::vector<std::string> removedGameIds;
		std::vector<std::string> removedSaveNamespaces;
	};

	struct SaveNamespaceInfo
	{
		std::string saveNamespace;
		std::string resourceName;
		std::uint64_t bytes = 0;
		int saveSlotCount = 0;
	};

	struct ResourcePack
	{
		std::string rootPath;     // 资源包根目录（归一化后以 '/' 结尾）
		std::string manifestPath; // game_profile.ini 相对路径
		ResourceManifest manifest;
		ModRelease::CompatibilityResult compatibility;
		// Stable catalog identity is independent from Game.Id so duplicate IDs
		// remain individually diagnosable even though conflicted entries cannot run.
		std::string catalogCollectionRoot;
		std::string catalogEntryKey;
		// Opaque discovery-source tag supplied by the shared catalog request.
		std::string sourceTag;
		// Stable identity presented by the shared catalog. It remains distinct
		// from Game.Id so colliding packs stay individually selectable.
		std::string selectionEntryKey;
		std::string effectiveSaveNamespace;
		bool saveNamespaceAdjusted = false;
		bool wasRecentlySelected = false;
		int discoveryOrder = 0;

		bool isBaseGame() const;
		bool isLaunchable() const;
		std::string getDisplayName() const;
		std::string getDisplayAuthorText() const;
	};

	static ResourceManager& instance();

	// 解析 --assets 参数并完成资源包发现/选择。
	// assetsArg 为空时使用平台默认 assets 路径作为集合目录。
	// 资源错误不应阻止普通游戏启动：零包或多包都会返回 true 并进入选择/
	// 管理界面；资源自身仍可因确定的版本或依赖问题被禁止进入。
	// 仅当 --assets/默认集合根本身不可解析（路径错误，而非资源错误）时返回 false。
	bool initialize(const std::string& assetsArg);

	// 获取当前 active resource root（归一化后以 '/' 结尾）。Android APK
	// asset namespace 的活动根本身可为空；调用方应使用 hasActiveResourceRoot()
	// 判断是否已有选择。
	const std::string& getActiveResourceRoot() const;

	// 获取当前 active manifest。若未选中，返回默认 manifest。
	const ResourceManifest& getActiveManifest() const;

	// 查询当前资源包的独立功能能力开关。
	bool isFeatureEnabled(const std::string& featureName, bool defaultValue = false) const;

	// 获取发现的所有资源包列表（用于调试/日志）。
	const std::vector<ResourcePack>& getDiscoveredPacks() const;

	// 获取本次资源集合扫描产生的诊断。资源选择页使用其中的错误项显示
	// 无效配置和重复 Game.Id，而不是让坏资源静默消失。
	const std::vector<RuntimeResource::CatalogDiagnostic>&
		getResourceCatalogDiagnostics() const;

	// 集合级更新入口以及旧版资源/主程序在线目录地址。新版优先通过
	// updateSourceUrl 解析有序目录列表，入口不可用时回退到旧版地址。
	const std::string& getUpdateSourceUrl() const;
	const std::string& getResourceCatalogUrl() const;
	const std::string& getApplicationCatalogUrl() const;

	// 返回当前平台用于在线安装和游戏内导入的可写资源集合。桌面端与
	// 主集合相同；Android/Apple 使用应用专属目录，可能因平台目录不可用
	// 而为空。它只描述物理写入位置，不区分本地资源和线上资源。
	const std::string& getWritableResourceCollectionRoot() const;

	// Builds an exact deletion list. Reverse dependencies are ordered before
	// their bases. A read-only/external dependent blocks the whole operation.
	bool isResourcePackRemovable(int packIndex) const;
	ResourceRemovalPlan buildResourceRemovalPlan(int packIndex) const;

	// No save behavior is implicit: Unselected is rejected. Resources are
	// deleted first; save namespaces are touched only after every resource was
	// removed successfully.
	ResourceRemovalResult removeResourceGroup(
		const ResourceRemovalPlan& plan,
		ResourceRemovalSavePolicy savePolicy);

	// Lists and removes namespace directories below the configured user save
	// root. This also exposes saves whose resource directory no longer exists.
	std::vector<SaveNamespaceInfo> listSaveNamespaces() const;
	ResourceRemovalResult removeSaveNamespaces(
		const std::vector<std::string>& saveNamespaces);

	// Refreshes discovery after a local download or deletion without changing
	// the configured collection roots.
	void rescanResources();

	// Activates a fully downloaded Ready transaction while the resource
	// selection page is still open, validates the switched group, and refreshes
	// discovery. It never selects or starts a game.
	bool activateStagedResourceInstall(
		std::string& errorText,
		bool allowUnplayableImportedResource = false);

	// 是否已确定 active resource selection。Android 打包资源的合法逻辑根
	// 可以为空，因此不能通过 getActiveResourceRoot().empty() 判断。
	bool hasActiveResourceRoot() const;

	// 供 ResourceSelectScene 在用户选择后调用，设置 active resource root 和 manifest。
	// compatibilityResult 非空时始终返回有效索引对应的缓存判定。
	bool setActiveResourcePack(int index,
		ModRelease::CompatibilityResult* compatibilityResult = nullptr);
	// 资源可以保留在选择列表中，但确定的版本、身份或依赖问题会禁止进入。
	bool canActivateResourcePack(
		int index, std::string* blockingReason = nullptr) const;

	// 记录资源选择页中由用户确认的资源包。记录保存在集合级
	// save 目录，不属于任何单个资源包或其存档命名空间。
	bool rememberResourcePackSelection(int index) const;

	// 按资源包 Id 设置 active resource root 和 manifest，供自动化启动/测试入口使用。
	bool setActiveResourcePackById(const std::string& id,
		ModRelease::CompatibilityResult* compatibilityResult = nullptr);

	// 重新扫描移动端固定的外部资源目录，并与主资源集合一起重新生成共享清单。
	// 供资源选择页在用户翻转"外部资源"开关后刷新列表使用。
	// 返回当前外部资源包数量。桌面端为空实现，返回 0。
	int rescanExternalResourceDirectory();

	// 是否需要显示资源选择/管理界面。任何未确定 active resource selection 的
	// 状态都返回 true（零包或多包未选择）；空逻辑根本身不代表未选择。
	bool needsSelection() const;

	// 直接安装 editor-run 准备出的资源路由。此入口不扫描目录、
	// 不读取 recent selection、也不写日志；成功后 initialize() 只消费已安装
	// 状态。必须在 File editor-run layout 安装之前调用。
	bool installEditorRunSelection(
		const RuntimeResource::ExactResourceSelection& selection);

private:
	friend class ResourceManagerPolicyTestAccess;

	ResourceManager() = default;
	~ResourceManager() = default;
	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;

	// 扫描集合目录，填充 discoveredPacks。
	void scanCollectionRoot(const std::string& collectionRoot);

	// 将移动端固定外部目录的直接子目录作为显式 supplemental roots 加入
	// 同一清单请求。清单解析器仅读取各子目录根部的 game_profile.ini。
	void appendExternalResourceCatalogRoots(
		RuntimeResource::ResourceCatalogRequest& request) const;
	void appendWritableResourceCatalogRoots(
		RuntimeResource::ResourceCatalogRequest& request) const;
	void applyCommonResourceRouting() const;

	// 归一化路径：替换 \ 为 /、折叠 . 与 ..，保留文件系统大小写并确保以 / 结尾。
	std::string normalizeRoot(const std::string& path) const;

	int findPackById(const std::string& id, int excludeIndex = -1) const;
	void promoteRecentResourcePackSelection();
	bool applyActiveResourcePack(int index);

	std::vector<ResourcePack> discoveredPacks;
	std::vector<RuntimeResource::CatalogDiagnostic>
		resourceCatalogDiagnostics;
	RuntimeResource::ResourceCatalogRequest currentCatalogRequest;
	// Android packaged assets legitimately use an empty logical primary root,
	// so request presence cannot be inferred from primaryCollectionRoot.empty().
	bool currentCatalogRequestValid = false;
	std::string assetsCollectionRoot;
	std::string writableResourceCollectionRoot;
	std::string commonResourceRoot;
	std::string writableCommonResourceRoot;
	std::string updateSourceUrl;
	std::string resourceCatalogUrl;
	std::string applicationCatalogUrl;
	std::string activeResourceRoot;
	// 活动选择状态独立于路径；空路径在 Android APK asset namespace 中是
	// 合法根。entry key 用于重扫后稳定恢复同一资源包。
	std::string activeResourceEntryKey;
	ResourceManifest activeManifest;
	bool activeResourceSelectionValid = false;
	bool initialized = false;
};
