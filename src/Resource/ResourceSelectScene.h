#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../Component/FlatTextButton.h"
#include "../Element/Element.h"
#include "../Engine/WindowTypes.h"
#include "ResourceManager.h"
#include "ResourcePackList.h"
#include "../Game/Menu/UIFocusManager.h"
#include "../Game/Loading/ExclusiveLoadingRunner.h"
#include "../Update/HttpsDownload.h"
#include "../Update/OnlineUpdateCatalog.h"
#include "../Update/ResourceDownloadPreparation.h"
#include "../Update/ResourceInstallTransaction.h"
#include "../Update/ResourcePackageArchive.h"

class GamepadEssentialUITestAccess;
class GamepadSurfaceContractTestAccess;

class ResourceSelectScene : public Element
{
	friend class GamepadEssentialUITestAccess;
	friend class GamepadSurfaceContractTestAccess;
public:
	ResourceSelectScene();
	virtual ~ResourceSelectScene();

	void freeResource();
	virtual void onChildCallBack(PElement child) override;

private:
	struct ResourceInstallConfirmationItem
	{
		std::string title;
		std::string version;
		std::string releaseNotes;
		std::string targetDirectoryName;
		bool replacing = false;
		OnlineUpdate::ResourceDownloadPlan::ArtifactKind artifactKind =
			OnlineUpdate::ResourceDownloadPlan::ArtifactKind::Full;
	};

	struct ResourceInstallConfirmation
	{
		std::string requestedGameId;
		std::string collectionRoot;
		std::uint64_t totalDownloadBytes = 0;
		bool requestedResourceInstalled = false;
		bool requestedVersionMatches = false;
		OnlineUpdate::RequestedResourceDownloadMode requestedDownloadMode =
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded;
		bool includesCommon = false;
		OnlineUpdate::InstalledResourceArtifactMap installedArtifacts;
		OnlineUpdate::InstalledResourceRootMap installedResourceRoots;
		std::vector<OnlineUpdate::ResourceInstallTarget> targets;
		std::vector<ResourceInstallConfirmationItem> items;
	};

	enum class ResourceInstallOperation
	{
		OnlineDownload,
		ProgramDownload,
		ResourceRemoval,
		SaveManagement
	};

	enum class ResourceInstallProgressStage
	{
		Downloading,
		ValidatingAndExtracting,
		Staging
	};

	struct ResourceInstallWorkerResult
	{
		ResourceInstallOperation operation =
			ResourceInstallOperation::OnlineDownload;
		std::atomic<std::uint64_t> completedBytes{ 0 };
		std::atomic<std::size_t> packageIndex{ 0 };
		std::atomic<ResourceInstallProgressStage> progressStage{
			ResourceInstallProgressStage::Downloading };
		std::size_t packageCount = 0;
		std::uint64_t totalBytes = 0;
		OnlineUpdate::ResourceDownloadPreparationResult preparation;
		OnlineUpdate::CommonDownloadPreparationResult commonPreparation;
		OnlineUpdate::ProgramDownloadPreparationResult programPreparation;
		OnlineUpdate::ResourcePackageArchiveResult programPackageResult;
		std::string preparedProgramPath;
		std::string programFailureMessage;
		bool programReady = false;
		OnlineUpdate::ResourceInstallTransactionResult transaction;
	};

	enum class ResourceInstallDialogState
	{
		Hidden,
		Confirming,
		BrowsingSaves,
		ConfirmingSaveRemoval,
		Downloading,
		Cancelling,
		ReadyToRestart,
		Completed,
		Failed
	};

	virtual bool onInitial();
	virtual void onDraw();
	virtual void onDrawEnd() override;
	virtual void onExit();
	virtual bool onHandleEvent(AEvent& event);
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onWindowResize(int width, int height) override;

	void createControls();
	void buildResourceList();
	void rebuildResourceEntries();
	void sortLocalResourceEntries(std::size_t localEntryCount);
	void sortOnlineOnlyResourceEntries(std::size_t firstOnlineEntry);
	void updateSelectedResourceDetails(int selectedIndex);
	void beginOnlineCatalogCheck();
	void pollOnlineCatalogCheck();
	void finishOnlineCatalogCheck(
		const GameLoading::ExclusiveLoadingCompletion& completion);
	void presentPendingProgramUpdateDialog();
	void refreshCheckUpdatesButton();
	void refreshProgramActionButton();
	void activateCheckUpdatesButton();
	void activateProgramActionButton();
	void refreshOnlineActionButton();
	void refreshResourceManagementButtons();
	bool beginResourceDownloadConfirmation(bool promptedByEntry = false);
	void beginProgramDownloadConfirmation();
	void beginResourceRemovalConfirmation();
	void beginSaveManagement();
	void executeResourceRemoval();
	void executeSaveRemoval();
	void activateResourceDialogPrimary();
	void activateResourceDialogSecondary();
	bool buildResourceInstallConfirmation(
		int selectedIndex,
		ResourceInstallConfirmation& confirmation,
		std::string& errorText,
		bool forceReinstallIfCurrent = true) const;
	void startConfirmedResourceDownload();
	void startPreparedProgramUpdate();
	void pollResourceInstall();
	void finishResourceInstall(
		const GameLoading::ExclusiveLoadingCompletion& completion);
	void cancelResourceInstall();
	void dismissResourceInstallDialog();
	void refreshResourceInstallDialogControls();
	void moveResourceInstallConfirmationPage(int offset);
	void configureFocus();
	bool navigateResourceSelection(UIFocusDirection direction);
	void moveSelection(int offset);
	void confirmSelection();
	void enterSelectedResource(int selectedIndex);
	void hideSemanticFocus();
	bool restoreSemanticFocus();
	void updateFocusPresentation();
	void synchronizeSemanticFocusWithInput();
	void loadSceneImages();
	void updateLayout(int width, int height);
	void updateControlLayout();
	void showCheatHelp(bool keepSemanticFocus);
	void hideCheatHelp(bool keepSemanticFocus);
	void showExternalResourceDialog(bool keepSemanticFocus);
	void hideExternalResourceDialog(bool keepSemanticFocus);
	void confirmExternalResourceDialog();
	void showDisplaySettings(bool keepSemanticFocus);
	void hideDisplaySettings(bool keepSemanticFocus);
	void refreshDisplaySettingsOptions();
	void rebuildDisplayResolutionOptions();
	void cycleDisplaySetting(int row, int offset);
	void applyPendingDisplaySettings();
	void resetPendingDisplaySettings();
	void setDisplaySettingsControlsVisible(bool visible);
	void updateDisplaySettingsControlLayout();
	void setMainControlsAvailable(bool available);
	Rect getExitButtonRect() const;
	Rect getCheatHelpButtonRect() const;
	Rect getCheckUpdatesButtonRect() const;
	Rect getDisplaySettingsButtonRect() const;
	Rect getProgramActionButtonRect() const;
	Rect getHeaderTitleRect() const;
	Rect getOnlineActionButtonRect() const;
	Rect getResourceRemoveButtonRect() const;
	Rect getSaveManagementButtonRect() const;
	Rect getDisplaySettingsRowValueRect(int row) const;
	Rect getDisplaySettingsPreviousButtonRect(int row) const;
	Rect getDisplaySettingsNextButtonRect(int row) const;
	Rect getDisplaySettingsApplyButtonRect() const;
	Rect getDisplaySettingsDefaultButtonRect() const;
	Rect getDisplaySettingsBackButtonRect() const;
	Rect getCheatHelpDialogRect() const;
	Rect getCheatHelpCloseButtonRect() const;
	Rect getExternalResourceDialogRect() const;
	Rect getExternalResourceConfirmButtonRect() const;
	Rect getExternalResourceCancelButtonRect() const;
	Rect getResourceInstallDialogRect() const;
	Rect getResourceInstallPrimaryButtonRect() const;
	Rect getResourceInstallSecondaryButtonRect() const;
	Rect getResourceInstallPreviousPageButtonRect() const;
	Rect getResourceInstallNextPageButtonRect() const;
	Rect getExternalLinkRect(int index) const;
	Rect getEnableExternalButtonRect() const;
	Rect getExternalResourcePathHintRect() const;
	Rect getCreditsTextAreaRect() const;
	Rect getScrollbarRect() const;
	void openExternalLink(int index);
	// 移动端"外部资源"开关点击处理：只有实际授权成功后才显示并保存为开启。
	void toggleExternalResources();
	void completeExternalPermissionRequest(bool permissionGranted);
	void refreshExternalResourcePresentation();
	void refreshExternalResourceDirectoryPath();
	// 实际执行外部资源重扫并刷新列表/焦点。
	void performExternalRescan();
	void drawBackground(int width, int height);
	void drawPanel();
	void drawDisplaySettingsPage();
	void drawCheatHelpOverlay();
	void drawExternalResourceOverlay();
	void drawResourceInstallOverlay();
	void drawResourceDetails();
	void drawCover();
	void drawTextLine(const std::string& text, int x, int y, int fontSize,
		unsigned int color, int maxWidth, int minimumFontSize = 10);
	void drawWrappedDescription(const std::string& text, const Rect& target,
		int fontSize, unsigned int color);

	struct CachedTextImage
	{
		_shared_image image = nullptr;
		int width = 0;
		int height = 0;
	};

	const CachedTextImage* getCachedTextImage(const std::string& text, int fontSize,
		unsigned int color, int maxWidth, int minimumFontSize);
	void drawCenteredText(const std::string& text, int centerX, int y, int fontSize,
		unsigned int color, int maxWidth = -1, int minimumFontSize = 14);

	static constexpr int ResourcePackItemHeight = 104;
	static constexpr int CompactMobileResourcePackItemHeight = 88;

	struct SelectedResourceDetails
	{
		int packIndex = -1;
		int localPackIndex = -1;
		std::string name;
		std::string resourceId;
		std::string version;
		std::string author;
		std::string releaseDate;
		std::string runStatus;
		std::string description;
		std::string onlineVersion;
		bool onlineAvailable = false;
		bool onlineOnly = false;
		bool onlineVersionMatches = false;
		bool hasPendingOnlineArtifacts = false;
		bool requiresNewerEngine = false;
		bool wasRecentlySelected = false;
		bool descriptionLoadedFromPack = false;
		bool coverLoadedFromPack = false;
	};

	struct ResourceSelectionEntry
	{
		int localPackIndex = -1;
		std::string gameId;
		std::string title;
		std::string author;
		std::string localVersion;
		std::string onlineVersion;
		std::string releaseNotes;
		std::string configurationErrorText;
		bool onlineAvailable = false;
		bool hasPendingOnlineArtifacts = false;
		bool requiresNewerEngine = false;
		bool wasRecentlySelected = false;
		bool configurationError = false;

		bool isOnlineOnly() const noexcept
		{
			return localPackIndex < 0 && !configurationError;
		}
	};

	struct CatalogCheckWorkerResult
	{
		using Endpoint = OnlineUpdate::CatalogMirrorSelectionResult;

		Endpoint resource;
		Endpoint application;
	};

	enum class CatalogCheckState
	{
		NotChecked,
		Checking,
		Ready,
		Failed
	};

	std::map<std::string, CachedTextImage> textCache;
	_shared_image backgroundImage = nullptr;
	_shared_image itemFrameImage = nullptr;
	_shared_image selectedItemFrameImage = nullptr;
	_shared_image detailCoverImage = nullptr;
	std::shared_ptr<ResourcePackList> resourceList;
	std::shared_ptr<FlatTextButton> exitButton;
	std::shared_ptr<FlatTextButton> cheatHelpButton;
	std::shared_ptr<FlatTextButton> checkUpdatesButton;
	std::shared_ptr<FlatTextButton> programActionButton;
	std::shared_ptr<FlatTextButton> onlineActionButton;
	std::shared_ptr<FlatTextButton> resourceRemoveButton;
	std::shared_ptr<FlatTextButton> saveManagementButton;
	std::shared_ptr<FlatTextButton> displaySettingsButton;
	std::array<std::shared_ptr<FlatTextButton>, 4>
		displaySettingsPreviousButtons;
	std::array<std::shared_ptr<FlatTextButton>, 4>
		displaySettingsNextButtons;
	std::shared_ptr<FlatTextButton> displaySettingsApplyButton;
	std::shared_ptr<FlatTextButton> displaySettingsDefaultButton;
	std::shared_ptr<FlatTextButton> displaySettingsBackButton;
	std::shared_ptr<FlatTextButton> cheatHelpCloseButton;
	std::shared_ptr<FlatTextButton> externalResourceConfirmButton;
	std::shared_ptr<FlatTextButton> externalResourceCancelButton;
	std::shared_ptr<FlatTextButton> resourceInstallPrimaryButton;
	std::shared_ptr<FlatTextButton> resourceInstallSecondaryButton;
	std::shared_ptr<FlatTextButton> resourceInstallPreviousPageButton;
	std::shared_ptr<FlatTextButton> resourceInstallNextPageButton;
	std::shared_ptr<FlatTextButton> enableExternalButton;
	std::vector<std::shared_ptr<FlatTextButton>> externalLinkButtons;
	UIFocusManager focusManager;
	int startY = 120;
	int startX = 80;
	int contentWidth = 0;
	int listHeight = 0;
	int panelX = 0;
	int panelY = 0;
	int panelWidth = 0;
	int panelHeight = 0;
	int panelPadding = 32;
	int minimumListHeight = 104;
	int narrowDetailHeight = 96;
	int detailGap = 12;
	Rect resourceListArea = { 0, 0, 0, 0 };
	Rect detailArea = { 0, 0, 0, 0 };
	Rect coverArea = { 0, 0, 0, 0 };
	bool wideDetailLayout = false;
	bool compactVerticalLayout = false;
	bool compactMobileLayout = false;
	SelectedResourceDetails selectedDetails;
	bool semanticFocusVisible = false;
	bool dispatchingKeyboardUIAction = false;
	bool keyboardSemanticFocus = false;
	bool cheatHelpVisible = false;
	bool externalResourceDialogVisible = false;
	bool displaySettingsVisible = false;
	std::vector<DesktopDisplayInfo> desktopDisplays;
	std::vector<DesktopDisplayResolution> displayResolutionOptions;
	DesktopDisplaySettings pendingDisplaySettings;
	std::string displaySettingsStatusText;
	std::vector<ResourceSelectionEntry> resourceEntries;
	OnlineUpdate::Catalog onlineCatalog;
	OnlineUpdate::Catalog onlineApplicationCatalog;
	OnlineUpdate::CatalogMirrorSources onlineResourceCatalogSources;
	OnlineUpdate::CatalogMirrorSources onlineApplicationCatalogSources;
	std::unique_ptr<GameLoading::ExclusiveLoadingRunner> catalogCheckRunner;
	std::shared_ptr<CatalogCheckWorkerResult> catalogCheckWorkerResult;
	CatalogCheckState catalogCheckState = CatalogCheckState::NotChecked;
	std::string catalogStatusText;
	bool programUpdateDialogPending = false;
	ResourceInstallDialogState resourceInstallDialogState =
		ResourceInstallDialogState::Hidden;
	ResourceInstallOperation resourceInstallOperation =
		ResourceInstallOperation::OnlineDownload;
	ResourceInstallConfirmation pendingResourceInstall;
	ResourceManager::ResourceRemovalPlan pendingResourceRemoval;
	ResourceManager::ResourceRemovalSavePolicy pendingResourceRemovalSavePolicy =
		ResourceManager::ResourceRemovalSavePolicy::Unselected;
	std::vector<ResourceManager::SaveNamespaceInfo> saveNamespaceEntries;
	int selectedSaveNamespaceIndex = 0;
	std::unique_ptr<GameLoading::ExclusiveLoadingRunner>
		resourceInstallRunner;
	std::shared_ptr<ResourceInstallWorkerResult>
		resourceInstallWorkerResult;
	std::string pendingProgramPackagePath;
	std::string resourceInstallDialogMessage;
	int resourceInstallConfirmationPage = 0;
	bool pendingDownloadUsesMeteredNetwork = false;
	bool pendingMeteredDownloadConfirmed = false;
	bool resourceUpdatePromptedByEntry = false;
	enum class ExternalResourcePresentationState
	{
		Disabled,
		PermissionRequired,
		WaitingForPermission,
		Enabled
	};
	ExternalResourcePresentationState externalResourcePresentationState =
		ExternalResourcePresentationState::Disabled;
	std::string externalResourceDirectoryPath;
	// 移动端：开关跳转权限设置页后，等待 Activity 明确报告授权流程完成。
	bool pendingExternalRescan = false;
};
