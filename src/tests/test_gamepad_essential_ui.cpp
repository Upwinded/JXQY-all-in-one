#include "../Component/Button.h"
#include "../Engine/Engine.h"
#include "../JxqyEngineVersion.h"
#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Game/Config/Config.h"
#include "../Game/Game.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Menu/ColumnMenu.h"
#include "../Game/Menu/ChooseMenu.h"
#include "../Game/Menu/ControllerHelpOverlay.h"
#include "../Game/Menu/ControllerPromptPresenter.h"
#include "../Game/Menu/Dialog.h"
#include "../Game/Menu/MenuSurfaceCatalog.h"
#include "../Game/Menu/MsgBox.h"
#include "../Game/Menu/NpcInfoPanel.h"
#include "../Game/Menu/Option.h"
#include "../Game/Menu/SaveLoad.h"
#include "../Game/Menu/StateMenu.h"
#include "../Game/Menu/SystemNotice.h"
#include "../Game/Menu/System.h"
#include "../Game/Menu/TimerMenu.h"
#include "../Game/Menu/ToolTip.h"
#include "../Game/Menu/TopMenu.h"
#include "../Game/Menu/YesNo.h"
#include "../Game/Scene/Title.h"
#include "../Game/Scene/TitleTeam.h"
#include "../Game/Scene/VideoPage.h"
#include "../Resource/ResourceManager.h"
#include "../Resource/ResourceSelectScene.h"
#include "HeadlessPhysicalInputTestHarness.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class GamepadEssentialUITestAccess
{
public:
	static bool dispatchControllerHelpEvent(
		ControllerHelpOverlay& overlay,
		AEvent event)
	{
		return static_cast<Element&>(overlay).handleEvent(event);
	}

	static bool controllerHelpIsRunning(
		const ControllerHelpOverlay& overlay)
	{
		return overlay.logicRunning;
	}

	static void resizeElementTree(Element& root, int width, int height)
	{
		root.resizeAll(width, height);
	}

	static bool prepareOrdinaryChoice(
		ChooseMenu& menu,
		const std::string& message,
		const std::vector<std::string>& options,
		const std::vector<bool>& visibleOptions)
	{
		ChooseMenu::SelectionConfiguration configuration;
		configuration.message = message;
		configuration.options = options;
		configuration.visibleOptions = visibleOptions;
		return menu.prepareSelection(configuration);
	}

	static bool prepareChoosePlus(
		ChooseMenu& menu,
		const std::string& message,
		const std::vector<std::string>& options,
		const std::vector<bool>& visibleOptions)
	{
		ChooseMenu::SelectionConfiguration configuration;
		configuration.message = message;
		configuration.options = options;
		configuration.visibleOptions = visibleOptions;
		configuration.choosePlus = true;
		configuration.speakerName = "Controller Test";
		configuration.dialogPosition = 0;
		return menu.prepareSelection(configuration);
	}

	static bool prepareMultipleChoice(
		ChooseMenu& menu,
		const std::string& message,
		const std::vector<std::string>& options,
		const std::vector<bool>& visibleOptions,
		int columnCount,
		int selectionCount)
	{
		ChooseMenu::SelectionConfiguration configuration;
		configuration.message = message;
		configuration.options = options;
		configuration.visibleOptions = visibleOptions;
		configuration.multiple = true;
		configuration.columnCount = columnCount;
		configuration.selectionCount = selectionCount;
		return menu.prepareSelection(configuration);
	}

	static PElement focusedTitleControl(const Title& title)
	{
		return title.focusManager.getFocusedElement();
	}

	static void previewTitlePointerEvent(Title& title, AEvent event)
	{
		title.onPreviewPointerEvent(event);
	}

	static const std::vector<AspectFitPointerRipple>& titlePointerRipples(
		const Title& title)
	{
		return title.pointerRipples;
	}

	static void removeExpiredTitlePointerRipples(Title& title)
	{
		title.removeExpiredPointerRipples();
	}

	static bool titleDrawsChildAfterComposition(
		const Title& title, const PElement& child)
	{
		return title.shouldDrawChildAfterComposition(child);
	}

	static std::shared_ptr<FadeMask> titleLoadingFadeMask(const Title& title)
	{
		return title.loadingFadeMask;
	}

	static std::shared_ptr<SystemNotice> titleSystemNotice(const Title& title)
	{
		return title.systemNotice;
	}

	static void showTitleSceneFailureNotice(
		Title& title,
		const std::string& failureMessage,
		int saveIndex,
		bool newGame,
		bool initializationFailure)
	{
		title.showSceneFailureNotice(
			failureMessage,
			saveIndex,
			newGame,
			initializationFailure);
	}

	static std::shared_ptr<Weather> titleWeather(const Title& title)
	{
		return title.weather;
	}

	static bool prepareVideoTitleTeam(TitleTeam& titleTeam)
	{
		return titleTeam.onInitial();
	}

	static bool isVideoOnlyTitleTeam(const TitleTeam& titleTeam)
	{
		return titleTeam.vp != nullptr
			&& titleTeam.vp->drawFullScreen
			&& titleTeam.teamInfoText.empty()
			&& titleTeam.children.size() == 1;
	}

	static bool isFullScreenVideoAndTextTitleTeam(const TitleTeam& titleTeam)
	{
		return titleTeam.vp != nullptr
			&& titleTeam.vp->drawFullScreen
			&& !titleTeam.teamInfoText.empty()
			&& titleTeam.children.size() == 1;
	}

	static bool isTitleTeamRunning(const TitleTeam& titleTeam)
	{
		return titleTeam.logicRunning;
	}

	static bool dispatchTitleTeamKey(TitleTeam& titleTeam, int key)
	{
		AEvent event(ET_KEYDOWN, key, 0, 0, false);
		return titleTeam.onHandleEvent(event);
	}

	static void updateTitleTeam(TitleTeam& titleTeam)
	{
		titleTeam.onUpdate();
	}

	static PElement focusedSaveLoadControl(const SaveLoad& saveLoad)
	{
		return saveLoad.focusManager.getFocusedElement();
	}

	static PElement focusedYesNoControl(const YesNo& yesNo)
	{
		return yesNo.focusManager.getFocusedElement();
	}

	static std::string focusedChoice(const ChooseMenu& menu)
	{
		return menu.focusManager.getFocusedNodeId();
	}

	static bool focusChoice(ChooseMenu& menu, const std::string& id)
	{
		return menu.focusManager.focusNode(id);
	}

	static bool dispatchChoiceKeyboardFrame(
		ChooseMenu& menu,
		int key,
		bool includeSyntheticMouseRefresh = false,
		bool repeat = false)
	{
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		AEvent keyDown(ET_KEYDOWN, key, 0, 0, repeat);
		engine->pushEvent(keyDown);
		if (includeSyntheticMouseRefresh)
		{
			if (menu.choiceButtons.empty()
				|| menu.choiceButtons.front() == nullptr)
			{
				return false;
			}
			const Rect& pointerRect = menu.choiceButtons.front()->rect;
			AEvent syntheticMouseRefresh(
				ET_MOUSEMOTION,
				TOUCH_MOUSEID,
				pointerRect.x + std::max(1, pointerRect.w / 2),
				pointerRect.y + std::max(1, pointerRect.h / 2),
				false,
				true);
			engine->pushEvent(syntheticMouseRefresh);
		}
		menu.allHandleEvents();
		return engine->getEventCount() == 0;
	}

	static bool choiceNavigationIndicatorMatchesFocus(
		const ChooseMenu& menu)
	{
		auto focusedButton = std::dynamic_pointer_cast<ChooseTextButton>(
			menu.focusManager.getFocusedElement());
		if (!menu.keyboardNavigationIndicatorVisible
			|| focusedButton == nullptr
			|| !focusedButton->isNavigationHighlighted()
			|| focusedButton->isFocused())
		{
			return false;
		}
		std::vector<const ChooseTextButton*> visited;
		int highlightedCount = 0;
		auto inspect =
			[&visited, &highlightedCount](
				const std::shared_ptr<ChooseTextButton>& button)
			{
				if (button == nullptr
					|| std::find(visited.begin(), visited.end(), button.get())
						!= visited.end())
				{
					return;
				}
				visited.push_back(button.get());
				if (button->isNavigationHighlighted())
				{
					highlightedCount++;
				}
			};
		for (const auto& button : menu.choiceButtons)
		{
			inspect(button);
		}
		inspect(menu.previousPageButton);
		inspect(menu.nextPageButton);
		inspect(menu.multipleClearButton);
		inspect(menu.multipleConfirmButton);
		return highlightedCount == 1;
	}

	static bool focusedChoiceIsSelectedAndNavigationHighlighted(
		const ChooseMenu& menu)
	{
		auto focusedButton = std::dynamic_pointer_cast<ChooseTextButton>(
			menu.focusManager.getFocusedElement());
		return focusedButton != nullptr
			&& focusedButton->isSelected()
			&& focusedButton->isNavigationHighlighted()
			&& !focusedButton->isFocused();
	}

	static bool choiceNavigationIndicatorVisible(const ChooseMenu& menu)
	{
		return menu.keyboardNavigationIndicatorVisible;
	}

	static bool dispatchChoicePointerFrame(
		ChooseMenu& menu, bool synthetic)
	{
		if (menu.choiceButtons.empty() || menu.choiceButtons.front() == nullptr)
		{
			return false;
		}
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		const Rect& pointerRect = menu.choiceButtons.front()->rect;
		AEvent pointerMotion(
			ET_MOUSEMOTION,
			TOUCH_MOUSEID,
			pointerRect.x + std::max(1, pointerRect.w / 2),
			pointerRect.y + std::max(1, pointerRect.h / 2),
			false,
			synthetic);
		engine->pushEvent(pointerMotion);
		menu.allHandleEvents();
		return engine->getEventCount() == 0;
	}

	static bool dispatchChoiceResizeFrame(
		ChooseMenu& menu, int width, int height)
	{
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		engine->setWindowSize(width, height);
		engine->pushEvent(AEvent(
			ET_WINDOWRESIZE, 0, width, height, false));
		menu.allHandleEvents();
		return engine->getEventCount() == 0;
	}

	static bool choiceSessionActive(const ChooseMenu& menu)
	{
		return menu.visible && menu.isInSelecting;
	}

	static int currentPageIndex(const ChooseMenu& menu)
	{
		return menu.currentPageIndex;
	}

	static int currentPageCount(const ChooseMenu& menu)
	{
		return menu.currentPageCount;
	}

	static std::vector<std::string> currentChoiceIds(const ChooseMenu& menu)
	{
		std::vector<std::string> ids;
		for (const auto& button : menu.choiceButtons)
		{
			if (button != nullptr && button->visible && button->activated)
			{
				ids.push_back("choice-" + std::to_string(button->index));
			}
		}
		return ids;
	}

	static void prepareResourceSelection(
		ResourceSelectScene& scene, int width, int height)
	{
		scene.freeResource();
		scene.rect = { 0, 0, width, height };
		scene.updateLayout(width, height);
		scene.createControls();
		scene.buildResourceList();
		scene.configureFocus();
	}

	static bool prepareProductionResourceSelection(ResourceSelectScene& scene)
	{
		ResourceManager::instance().setActiveResourcePack(0);
		return scene.onInitial();
	}

	static bool automaticCatalogCheckStarted(
		const ResourceSelectScene& scene)
	{
		return scene.catalogCheckState !=
			ResourceSelectScene::CatalogCheckState::NotChecked;
	}

	static int selectedResourceIndex(const ResourceSelectScene& scene)
	{
		return scene.resourceList != nullptr
			? scene.resourceList->getSelectedIndex() : -1;
	}

	static int selectedResourceDetailIndex(
		const ResourceSelectScene& scene)
	{
		return scene.selectedDetails.packIndex;
	}

	static bool resourceDetailMatchesSelection(
		const ResourceSelectScene& scene, int selectedIndex)
	{
		return scene.selectedDetails.packIndex == selectedIndex;
	}

	static std::string selectedResourceDescription(
		const ResourceSelectScene& scene)
	{
		return scene.selectedDetails.description;
	}

	static bool selectedResourceDetailsContainSecondaryMetadata(
		const ResourceSelectScene& scene,
		const ResourceManager::ResourcePack& pack)
	{
		const std::string expectedId = pack.manifest.id.empty()
			? std::string(u8"未声明") : pack.manifest.id;
		const std::string expectedReleaseDate =
			pack.manifest.releaseMetadata.releaseDate.empty()
				? std::string(u8"未声明")
				: pack.manifest.releaseMetadata.releaseDate;
		return scene.selectedDetails.resourceId == expectedId
			&& scene.selectedDetails.releaseDate == expectedReleaseDate
			&& !scene.selectedDetails.runStatus.empty()
			&& scene.selectedDetails.wasRecentlySelected
				== pack.wasRecentlySelected;
	}

	static bool selectedResourceDescriptionLoadedFromPack(
		const ResourceSelectScene& scene)
	{
		return scene.selectedDetails.descriptionLoadedFromPack;
	}

	static bool selectedResourceUsesCoverPlaceholder(
		const ResourceSelectScene& scene)
	{
		return !scene.selectedDetails.coverLoadedFromPack;
	}

	static bool resourceDetailsUseWideLayout(
		const ResourceSelectScene& scene)
	{
		return scene.wideDetailLayout;
	}

	static Rect resourceDetailRect(const ResourceSelectScene& scene)
	{
		return scene.detailArea;
	}

	static Rect resourceCoverRect(const ResourceSelectScene& scene)
	{
		return scene.coverArea;
	}

	static bool resourceHeaderActionsAreLarge(
		const ResourceSelectScene& scene)
	{
		if (scene.exitButton == nullptr || scene.cheatHelpButton == nullptr ||
			scene.checkUpdatesButton == nullptr ||
			scene.saveManagementButton == nullptr)
		{
			return false;
		}
		const Rect& exitRect = scene.exitButton->rect;
		const Rect& cheatHelpRect = scene.cheatHelpButton->rect;
		const Rect& saveManagementRect = scene.saveManagementButton->rect;
		const Rect& checkUpdatesRect = scene.checkUpdatesButton->rect;
#if !defined(__MOBILE__)
		if (scene.displaySettingsButton == nullptr)
		{
			return false;
		}
		const Rect& displaySettingsRect = scene.displaySettingsButton->rect;
#endif
		const Rect titleRect = scene.getHeaderTitleRect();
		const int rightInset =
			scene.panelX + scene.panelWidth
			- (exitRect.x + exitRect.w);
		const int leftInset = cheatHelpRect.x - scene.panelX;
		const bool compact = scene.panelWidth < 664;
		const int buttonCount = 5;
		const int gap = compact ? 6 : 10;
		const int expectedButtonWidth = std::min(
			compact ? 88 : 108,
			std::max(1,
				(scene.contentWidth - gap * (buttonCount - 1)) / buttonCount));
		const int expectedCheckButtonWidth = compact
			? expectedButtonWidth : 128;
		const int expectedFontSize = compact ? 12 : 17;
		const Rect externalLinkRect = scene.getExternalLinkRect(0);
		return exitRect.w == expectedButtonWidth
			&& exitRect.h == 36
			&& exitRect.h == externalLinkRect.h
			&& rightInset == scene.panelPadding
			&& leftInset == scene.panelPadding
			&& cheatHelpRect.w == exitRect.w
			&& cheatHelpRect.h == exitRect.h
			&& cheatHelpRect.y == exitRect.y
			&& saveManagementRect.w == exitRect.w
			&& saveManagementRect.h == exitRect.h
			&& saveManagementRect.y == exitRect.y
			&& checkUpdatesRect.w == expectedCheckButtonWidth
			&& checkUpdatesRect.h == exitRect.h
			&& checkUpdatesRect.y == exitRect.y
			&& scene.exitButton->getFontSize() == expectedFontSize
			&& scene.cheatHelpButton->getFontSize() == expectedFontSize
			&& scene.saveManagementButton->getFontSize() == expectedFontSize
			&& scene.checkUpdatesButton->getFontSize() == expectedFontSize
			&& cheatHelpRect.x + cheatHelpRect.w <= saveManagementRect.x
#if !defined(__MOBILE__)
			&& displaySettingsRect.w == exitRect.w
			&& displaySettingsRect.h == exitRect.h
			&& displaySettingsRect.y == exitRect.y
			&& scene.displaySettingsButton->getFontSize() == expectedFontSize
			&& saveManagementRect.x + saveManagementRect.w <=
				displaySettingsRect.x
			&& displaySettingsRect.x + displaySettingsRect.w <=
				checkUpdatesRect.x
#else
			&& saveManagementRect.x + saveManagementRect.w <=
				checkUpdatesRect.x
#endif
			&& checkUpdatesRect.x + checkUpdatesRect.w <= exitRect.x
			&& titleRect.x == scene.panelX + scene.panelPadding
			&& titleRect.w > 0
			&& titleRect.x + titleRect.w <=
				scene.panelX + scene.panelPadding + scene.contentWidth
			&& titleRect.y + titleRect.h <= exitRect.y
			&& exitRect.y + exitRect.h < scene.startY;
	}

	static bool presentProgramUpdateAction(ResourceSelectScene& scene)
	{
		OnlineUpdate::ProgramPackage package;
		package.target = JxqyBuildVersion::ProgramUpdateTarget;
		package.versionText = "9.9.0-test";
		package.version = ModRelease::parseSemanticVersion(
			package.versionText).version;
		package.artifactPath = "program/test.zip";
		package.artifactSize = 1024;
		package.crc32Hex = "00000000";
		scene.onlineApplicationCatalog.programPackages.emplace(
			package.target, std::move(package));
		scene.catalogCheckState = ResourceSelectScene::CatalogCheckState::Ready;
		scene.refreshCheckUpdatesButton();
		scene.configureFocus();
		return scene.checkUpdatesButton != nullptr &&
			scene.checkUpdatesButton->visible &&
			scene.checkUpdatesButton->activated &&
			scene.programActionButton != nullptr &&
			scene.programActionButton->visible &&
			scene.programActionButton->activated;
	}

	static bool programActionStaysHiddenDuringModalRefresh(
		ResourceSelectScene& scene)
	{
		if (!presentProgramUpdateAction(scene))
		{
			return false;
		}
		scene.showCheatHelp(false);
		scene.refreshCheckUpdatesButton();
		const bool hiddenDuringModal = scene.cheatHelpVisible &&
			scene.programActionButton != nullptr &&
			!scene.programActionButton->visible &&
			!scene.programActionButton->activated;
		scene.hideCheatHelp(false);
		bool hiddenDuringExternalDialog = true;
#if defined(__ANDROID__) || \
	defined(JXQY_TEST_ANDROID_EXTERNAL_RESOURCE_UI)
		scene.showExternalResourceDialog(false);
		scene.refreshCheckUpdatesButton();
		hiddenDuringExternalDialog = scene.externalResourceDialogVisible &&
			!scene.programActionButton->visible &&
			!scene.programActionButton->activated;
		scene.hideExternalResourceDialog(false);
#endif
		return hiddenDuringModal && hiddenDuringExternalDialog &&
			scene.programActionButton->visible &&
			scene.programActionButton->activated;
	}

	static bool completeAutomaticProgramUpdateCheck(
		ResourceSelectScene& scene,
		const std::string& onlineVersion,
		bool expectDialog)
	{
		OnlineUpdate::ProgramPackage package;
		package.target = JxqyBuildVersion::ProgramUpdateTarget;
		package.versionText = onlineVersion;
		package.version = ModRelease::parseSemanticVersion(
			package.versionText).version;
		package.artifactPath = "program/test.zip";
		package.artifactSize = 1024;
		package.crc32Hex = "00000000";
		package.releaseNotes = "Current-platform program update fixture";
		const std::string expectedReleaseNotes = package.releaseNotes;
		OnlineUpdate::ProgramPackage otherPlatformPackage = package;
		otherPlatformPackage.target = package.target == "windows"
			? "linux" : "windows";
		otherPlatformPackage.releaseNotes =
			"Other-platform program update fixture";

		scene.catalogCheckWorkerResult =
			std::make_shared<ResourceSelectScene::CatalogCheckWorkerResult>();
		auto& application = scene.catalogCheckWorkerResult->application;
		application.configured = true;
		application.downloadAttempted = true;
		application.download.status =
			OnlineUpdate::HttpsDownloadStatus::Success;
		application.sources.catalogUrls = {
			"https://updates.example.test/application/catalog.ini" };
		application.parse.catalog.programPackages.emplace(
			package.target, std::move(package));
		application.parse.catalog.programPackages.emplace(
			otherPlatformPackage.target, std::move(otherPlatformPackage));
		GameLoading::ExclusiveLoadingCompletion completion;
		completion.taskResult = GameLoading::LoadingTaskResult::success();
		scene.finishOnlineCatalogCheck(completion);

		const bool manualActionRemainsAvailable =
			scene.programActionButton != nullptr &&
			scene.programActionButton->visible &&
			scene.programActionButton->activated;
		const bool waitingForPresentation =
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Hidden &&
			scene.programUpdateDialogPending == expectDialog;
		scene.presentPendingProgramUpdateDialog();
		if (!expectDialog)
		{
			return manualActionRemainsAvailable && waitingForPresentation &&
				!scene.programUpdateDialogPending &&
				scene.resourceInstallDialogState ==
					ResourceSelectScene::ResourceInstallDialogState::Hidden;
		}
		return manualActionRemainsAvailable && waitingForPresentation &&
			!scene.programUpdateDialogPending &&
			scene.resourceInstallOperation ==
				ResourceSelectScene::ResourceInstallOperation::ProgramDownload &&
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Confirming &&
			scene.resourceInstallPrimaryButton != nullptr &&
			scene.resourceInstallPrimaryButton->visible &&
			scene.resourceInstallPrimaryButton->activated &&
			scene.resourceInstallSecondaryButton != nullptr &&
			scene.resourceInstallSecondaryButton->visible &&
			scene.resourceInstallSecondaryButton->activated &&
			scene.pendingResourceInstall.requestedGameId ==
				JxqyBuildVersion::ProgramUpdateTarget &&
			scene.pendingResourceInstall.items.size() == 1 &&
			scene.pendingResourceInstall.items.front().releaseNotes ==
				expectedReleaseNotes &&
			focusedResourceControl(scene) == "install-secondary";
	}

	static bool presentCurrentOnlineProgramDownload(ResourceSelectScene& scene)
	{
		OnlineUpdate::ProgramPackage package;
		package.target = JxqyBuildVersion::ProgramUpdateTarget;
		package.versionText = JxqyBuildVersion::EngineVersion;
		package.version = ModRelease::parseSemanticVersion(
			package.versionText).version;
		package.artifactPath = "program/current.zip";
		package.artifactSize = 1024;
		package.crc32Hex = "00000000";
		scene.onlineApplicationCatalog.programPackages.emplace(
			package.target, std::move(package));
		scene.catalogCheckState = ResourceSelectScene::CatalogCheckState::Ready;
		scene.refreshCheckUpdatesButton();
		const bool actionStarted = activateProgramUpdate(scene);
		return actionStarted &&
			scene.catalogCheckState ==
				ResourceSelectScene::CatalogCheckState::Ready &&
			scene.catalogCheckRunner == nullptr;
	}

	static bool programUpdateActionIsContained(
		const ResourceSelectScene& scene)
	{
		if (scene.checkUpdatesButton == nullptr ||
			scene.programActionButton == nullptr ||
			scene.cheatHelpButton == nullptr)
		{
			return false;
		}
		const Rect& check = scene.checkUpdatesButton->rect;
		const Rect& program = scene.programActionButton->rect;
		if (scene.compactMobileLayout)
		{
			const Rect& save = scene.saveManagementButton->rect;
			return program.w > 0 && program.h > 0 &&
				program.y == check.y && program.h == check.h &&
				save.x + save.w <= program.x &&
				program.x + program.w <= check.x &&
				scene.programActionButton->getFontSize() ==
					scene.checkUpdatesButton->getFontSize();
		}
		return program.w > 0 && program.h > 0 &&
			program.x >= scene.panelX + scene.panelPadding &&
			program.x + program.w <=
				scene.panelX + scene.panelWidth - scene.panelPadding &&
			check.y + check.h <= program.y &&
			program.y + program.h < scene.startY;
	}

	static bool activateProgramUpdate(ResourceSelectScene& scene)
	{
		scene.catalogStatusText.clear();
		scene.activateProgramActionButton();
		return scene.catalogCheckState ==
				ResourceSelectScene::CatalogCheckState::Ready &&
			scene.catalogCheckRunner == nullptr &&
			(scene.resourceInstallOperation ==
					ResourceSelectScene::ResourceInstallOperation::ProgramDownload ||
				!scene.catalogStatusText.empty());
	}

	static bool activateProgramUpdateWithPointer(ResourceSelectScene& scene)
	{
		scene.catalogStatusText.clear();
		return dispatchResourceControlClickAcrossLayout(
				scene, scene.programActionButton, false) &&
			scene.resourceInstallOperation ==
				ResourceSelectScene::ResourceInstallOperation::ProgramDownload &&
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Confirming;
	}

	static bool navigateResourceSelectionToUpdate(
		ResourceSelectScene& scene)
	{
		if (!scene.handleUIAction(UIAction::NavigateRight))
		{
			return false;
		}
		if (focusedResourceControl(scene) == "resource-remove" &&
			!scene.handleUIAction(UIAction::NavigateRight))
		{
			return false;
		}
		if (focusedResourceControl(scene) == "save-management" &&
			!scene.handleUIAction(UIAction::NavigateRight))
		{
			return false;
		}
		if (focusedResourceControl(scene) == "display-settings" &&
			!scene.handleUIAction(UIAction::NavigateRight))
		{
			return false;
		}
		return focusedResourceControl(scene) == "check-updates";
	}

	static bool displaySettingsPageIsUsable(ResourceSelectScene& scene)
	{
#if defined(__MOBILE__)
		return scene.displaySettingsButton == nullptr;
#else
		if (scene.displaySettingsButton == nullptr)
		{
			return false;
		}
		const std::vector<DesktopDisplayInfo> availableDisplays =
			Engine::getInstance()->getDesktopDisplays();
		if (!availableDisplays.empty())
		{
			if (!scene.handleUIAction(UIAction::NavigateUp) ||
			focusedResourceControl(scene) != "check-updates" ||
			!scene.handleUIAction(UIAction::NavigateLeft) ||
			focusedResourceControl(scene) != "display-settings" ||
			!scene.handleUIAction(UIAction::Confirm))
			{
				return false;
			}
		}
		else
		{
			DesktopDisplayInfo testDisplay;
			testDisplay.name = "Test Display";
			testDisplay.desktopWidth = 1920;
			testDisplay.desktopHeight = 1080;
			testDisplay.usableWidth = 1920;
			testDisplay.usableHeight = 1040;
			testDisplay.fullscreenResolutions =
			{
				{ 640, 480 }, { 1280, 720 }, { 1920, 1080 }
			};
			scene.desktopDisplays = { testDisplay };
			scene.pendingDisplaySettings = {};
			scene.displaySettingsVisible = true;
			scene.setMainControlsAvailable(false);
			scene.refreshDisplaySettingsOptions();
			scene.setDisplaySettingsControlsVisible(true);
			scene.focusManager.focusNode("display-settings-next-1");
		}
		if (!scene.displaySettingsVisible || scene.resourceList == nullptr ||
			scene.resourceList->visible || scene.desktopDisplays.empty() ||
			scene.displayResolutionOptions.empty() ||
			(focusedResourceControl(scene) != "display-settings-next-0" &&
				focusedResourceControl(scene) != "display-settings-next-1") ||
			scene.displaySettingsApplyButton == nullptr ||
			!scene.displaySettingsApplyButton->visible ||
			scene.pendingDisplaySettings.width < 640 ||
			scene.pendingDisplaySettings.height < 480)
		{
			return false;
		}
		for (int row = 0; row < 4; row++)
		{
			if (scene.displaySettingsPreviousButtons[row] == nullptr ||
				scene.displaySettingsNextButtons[row] == nullptr ||
				!scene.displaySettingsPreviousButtons[row]->visible ||
				!scene.displaySettingsNextButtons[row]->visible)
			{
				return false;
			}
		}
		scene.pendingDisplaySettings.fullScreenMode = FullScreenMode::window;
		scene.pendingDisplaySettings.fullScreenSolutionMode =
			FullScreenSolutionMode::original;
		scene.cycleDisplaySetting(1, 1);
		if (scene.pendingDisplaySettings.fullScreenMode !=
				FullScreenMode::windowFullScreen ||
			scene.pendingDisplaySettings.fullScreenSolutionMode !=
				FullScreenSolutionMode::adjust)
		{
			return false;
		}
		scene.cycleDisplaySetting(3, 1);
		if (scene.pendingDisplaySettings.fullScreenSolutionMode !=
			FullScreenSolutionMode::forceToUseSetting)
		{
			return false;
		}
		scene.cycleDisplaySetting(3, -1);
		if (scene.pendingDisplaySettings.fullScreenSolutionMode !=
			FullScreenSolutionMode::adjust)
		{
			return false;
		}
		return scene.handleUIAction(UIAction::Cancel) &&
			!scene.displaySettingsVisible && scene.resourceList->visible &&
			focusedResourceControl(scene) == "display-settings";
#endif
	}

	static bool applyOnlineCatalog(
		ResourceSelectScene& scene,
		OnlineUpdate::Catalog catalog)
	{
		scene.onlineCatalog = std::move(catalog);
		scene.catalogCheckState =
			ResourceSelectScene::CatalogCheckState::Ready;
		scene.buildResourceList();
		scene.configureFocus();
		return scene.resourceList != nullptr;
	}

	static int resourceEntryCount(const ResourceSelectScene& scene)
	{
		return static_cast<int>(scene.resourceEntries.size());
	}

	static int resourceEntryLocalPackIndex(
		const ResourceSelectScene& scene, int entryIndex)
	{
		return entryIndex >= 0 &&
			entryIndex < static_cast<int>(scene.resourceEntries.size())
			? scene.resourceEntries[entryIndex].localPackIndex
			: -1;
	}

	static int resourceEntryIndexByGameId(
		const ResourceSelectScene& scene, const std::string& gameId)
	{
		const std::string foldedGameId = OnlineUpdate::foldGameId(gameId);
		for (int index = 0;
			index < static_cast<int>(scene.resourceEntries.size()); index++)
		{
			if (OnlineUpdate::foldGameId(
					scene.resourceEntries[index].gameId) == foldedGameId)
			{
				return index;
			}
		}
		return -1;
	}

	static ResourceSelectScene::ResourceSelectionEntry resourceEntry(
		const std::string& gameId,
		int localPackIndex = -1,
		bool wasRecentlySelected = false)
	{
		ResourceSelectScene::ResourceSelectionEntry entry;
		entry.localPackIndex = localPackIndex;
		entry.gameId = gameId;
		entry.wasRecentlySelected = wasRecentlySelected;
		return entry;
	}

	static bool resourceEntryOrderMatches(
		const ResourceSelectScene& scene,
		const std::vector<std::string>& expectedOrder)
	{
		if (scene.resourceEntries.size() != expectedOrder.size())
		{
			return false;
		}
		for (std::size_t index = 0; index < expectedOrder.size(); index++)
		{
			if (scene.resourceEntries[index].gameId != expectedOrder[index])
			{
				return false;
			}
		}
		return true;
	}

	static bool localResourcesUsePreferredDisplayOrder()
	{
		ResourceSelectScene scene;
		scene.resourceEntries =
		{
			resourceEntry("A_MOD", 0),
			resourceEntry("XJXQY", 1),
			resourceEntry("YYCS", 2),
			resourceEntry("RECENT_MOD", 3, true),
			resourceEntry("jxqy2", 4),
			resourceEntry("Z_MOD", 5)
		};

		scene.sortLocalResourceEntries(scene.resourceEntries.size());
		return resourceEntryOrderMatches(
			scene,
			{
				"RECENT_MOD",
				"jxqy2",
				"YYCS",
				"XJXQY",
				"A_MOD",
				"Z_MOD"
			});
	}

	static bool onlineOnlyResourcesUsePreferredDisplayOrder()
	{
		ResourceSelectScene scene;
		scene.resourceEntries =
		{
			resourceEntry("INSTALLED_MOD", 0),
			resourceEntry("Z_MOD"),
			resourceEntry("XJXQY"),
			resourceEntry("A_MOD"),
			resourceEntry("jxqy2"),
			resourceEntry("YYCS")
		};

		scene.sortOnlineOnlyResourceEntries(1);
		return resourceEntryOrderMatches(
			scene,
			{
				"INSTALLED_MOD",
				"jxqy2",
				"YYCS",
				"XJXQY",
				"Z_MOD",
				"A_MOD"
			});
	}

	static bool presentResourceConfigurationError(
		ResourceSelectScene& scene)
	{
		if (scene.resourceList == nullptr)
		{
			return false;
		}
		ResourceSelectScene::ResourceSelectionEntry entry;
		entry.title = "broken-resource";
		entry.configurationError = true;
		entry.configurationErrorText =
			u8"无法读取有效的 game_profile.ini";
		scene.resourceEntries = {entry};
		ResourcePackCardContent card;
		card.title = entry.title;
		card.authorAndVersion = u8"资源配置错误 · 不能进入";
		scene.resourceList->setItems({card});
		scene.resourceList->setSelectedIndex(0);
		scene.updateSelectedResourceDetails(0);
		return scene.selectedDetails.runStatus ==
				u8"资源配置错误，不能进入" &&
			!scene.selectedDetails.onlineAvailable &&
			(scene.onlineActionButton == nullptr ||
				!scene.onlineActionButton->visible);
	}

	static bool configurationErrorCannotEnter(
		ResourceSelectScene& scene)
	{
		scene.setRunning(true);
		scene.confirmSelection();
		return scene.logicRunning &&
			scene.catalogStatusText.find(u8"资源配置错误") !=
				std::string::npos;
	}

	static void confirmSelectedResource(ResourceSelectScene& scene)
	{
		scene.confirmSelection();
	}

	static bool selectedResourceHasOnlineVersion(
		const ResourceSelectScene& scene,
		const std::string& version,
		bool onlineOnly)
	{
		return scene.selectedDetails.onlineAvailable &&
			scene.selectedDetails.onlineOnly == onlineOnly &&
			scene.selectedDetails.onlineVersion == version;
	}

	static bool selectedResourceMatchesOnlineVersion(
		const ResourceSelectScene& scene)
	{
		if (!scene.selectedDetails.onlineVersionMatches ||
			scene.resourceList == nullptr)
		{
			return false;
		}
		const int selectedIndex = scene.resourceList->getSelectedIndex();
		const std::string expectedAuthor = scene.selectedDetails.author.empty()
			? std::string(u8"作者：未声明") : scene.selectedDetails.author;
		return selectedIndex >= 0 &&
			selectedIndex < static_cast<int>(scene.resourceList->cards.size()) &&
			scene.resourceList->cards[selectedIndex] != nullptr &&
			scene.selectedDetails.runStatus.find(u8"已与线上版本一致") !=
				std::string::npos &&
			scene.resourceList->cards[selectedIndex]->content.authorAndVersion.find(
				expectedAuthor) != std::string::npos &&
			scene.resourceList->cards[selectedIndex]->content.authorAndVersion.find(
				u8"线上") == std::string::npos;
	}

	static bool resourceOnlineActionIsAvailable(
		const ResourceSelectScene& scene)
	{
		return scene.onlineActionButton != nullptr &&
			scene.onlineActionButton->visible &&
			 scene.onlineActionButton->activated;
	}

	static bool selectedResourceNeedsContinueUpdate(
		const ResourceSelectScene& scene)
	{
		return scene.selectedDetails.hasPendingOnlineArtifacts &&
			scene.selectedDetails.runStatus.find(
				u8"资源内容仍需更新") != std::string::npos &&
			resourceOnlineActionIsAvailable(scene);
	}

	static bool resourceRemovalRequiresExplicitSaveChoice(
		ResourceSelectScene& scene)
	{
		scene.resourceInstallOperation =
			ResourceSelectScene::ResourceInstallOperation::ResourceRemoval;
		scene.resourceInstallDialogState =
			ResourceSelectScene::ResourceInstallDialogState::Confirming;
		scene.pendingResourceRemoval = {};
		scene.pendingResourceRemoval.status =
			ResourceManager::ResourceRemovalStatus::Success;
		ResourceManager::ResourceRemovalEntry entry;
		entry.gameId = "TEST";
		entry.name = "Test Resource";
		entry.saveNamespace = "test";
		entry.saveExists = true;
		scene.pendingResourceRemoval.entries.push_back(std::move(entry));
		scene.pendingResourceRemovalSavePolicy =
			ResourceManager::ResourceRemovalSavePolicy::Unselected;
		scene.setMainControlsAvailable(false);
		scene.refreshResourceInstallDialogControls();
		const bool initiallyUnselected =
			scene.resourceInstallPrimaryButton != nullptr &&
			scene.resourceInstallPrimaryButton->visible &&
			!scene.resourceInstallPrimaryButton->activated &&
			scene.resourceInstallPreviousPageButton != nullptr &&
			scene.resourceInstallPreviousPageButton->visible &&
			scene.resourceInstallPreviousPageButton->activated &&
			scene.resourceInstallNextPageButton != nullptr &&
			scene.resourceInstallNextPageButton->visible &&
			scene.resourceInstallNextPageButton->activated;
		scene.moveResourceInstallConfirmationPage(-1);
		const bool explicitChoiceEnablesConfirmation =
			scene.pendingResourceRemovalSavePolicy ==
				ResourceManager::ResourceRemovalSavePolicy::Delete &&
			scene.resourceInstallPrimaryButton->activated;
		scene.dismissResourceInstallDialog();
		return initiallyUnselected && explicitChoiceEnablesConfirmation &&
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Hidden;
	}

	static bool meteredDownloadRequiresSecondConfirmation(
		ResourceSelectScene& scene)
	{
		scene.resourceInstallOperation =
			ResourceSelectScene::ResourceInstallOperation::OnlineDownload;
		scene.resourceInstallDialogState =
			ResourceSelectScene::ResourceInstallDialogState::Confirming;
		scene.pendingResourceInstall.collectionRoot.clear();
		scene.pendingDownloadUsesMeteredNetwork = true;
		scene.pendingMeteredDownloadConfirmed = false;
		scene.refreshResourceInstallDialogControls();
		scene.activateResourceDialogPrimary();
		const bool firstClickOnlyWarns =
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Confirming &&
			scene.pendingMeteredDownloadConfirmed &&
			scene.resourceInstallRunner == nullptr;
		scene.activateResourceDialogSecondary();
		const bool backReturnsToFirstConfirmation =
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Confirming &&
			!scene.pendingMeteredDownloadConfirmed;
		scene.dismissResourceInstallDialog();
		return firstClickOnlyWarns && backReturnsToFirstConfirmation;
	}

	static bool beginSelectedResourceDownloadConfirmation(
		ResourceSelectScene& scene)
	{
		scene.beginResourceDownloadConfirmation();
		return scene.resourceInstallDialogState ==
			ResourceSelectScene::ResourceInstallDialogState::Confirming;
	}

	static bool resourceInstallConfirmationContains(
		const ResourceSelectScene& scene,
		const std::string& gameId,
		const std::string& targetDirectoryName,
		bool replacing)
	{
		for (std::size_t index = 0;
			index < scene.pendingResourceInstall.targets.size() &&
				index < scene.pendingResourceInstall.items.size(); index++)
		{
			if (scene.pendingResourceInstall.targets[index].gameId == gameId &&
				scene.pendingResourceInstall.targets[index].targetDirectoryName ==
					targetDirectoryName &&
				scene.pendingResourceInstall.items[index].replacing == replacing)
			{
				return true;
			}
		}
		return false;
	}

	static int resourceInstallConfirmationItemCount(
		const ResourceSelectScene& scene)
	{
		return static_cast<int>(scene.pendingResourceInstall.items.size());
	}

	static bool resourceInstallConfirmationContainsReleaseNotes(
		const ResourceSelectScene& scene,
		const std::string& gameId,
		const std::string& releaseNotes)
	{
		for (std::size_t index = 0;
			index < scene.pendingResourceInstall.targets.size() &&
				index < scene.pendingResourceInstall.items.size(); index++)
		{
			if (scene.pendingResourceInstall.targets[index].gameId == gameId &&
				scene.pendingResourceInstall.items[index].releaseNotes ==
					releaseNotes)
			{
				return true;
			}
		}
		return false;
	}

	static bool resourceInstallConfirmationUsesOnePagePerItem(
		ResourceSelectScene& scene)
	{
		if (scene.pendingResourceInstall.items.size() < 2)
		{
			return false;
		}
		scene.resourceInstallConfirmationPage = 0;
		scene.moveResourceInstallConfirmationPage(1);
		const bool advancedOneItem =
			scene.resourceInstallConfirmationPage == 1;
		scene.moveResourceInstallConfirmationPage(-1);
		return advancedOneItem && scene.resourceInstallConfirmationPage == 0;
	}

	static bool resourceInstallConfirmationKind(
		const ResourceSelectScene& scene,
		bool installed,
		bool sameVersion,
		bool includesCommon)
	{
		return scene.pendingResourceInstall.requestedResourceInstalled ==
				installed &&
			scene.pendingResourceInstall.requestedVersionMatches == sameVersion &&
			scene.pendingResourceInstall.includesCommon == includesCommon;
	}

	static bool resourceInstallConfirmationUsesIncrementalOnly(
		const ResourceSelectScene& scene)
	{
		return scene.pendingResourceInstall.items.size() == 1 &&
			scene.pendingResourceInstall.items.front().artifactKind ==
				OnlineUpdate::ResourceDownloadPlan::ArtifactKind::Incremental;
	}

	static bool localEntryUpdatePromptIsVisible(
		const ResourceSelectScene& scene)
	{
		return scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Confirming &&
			scene.resourceInstallOperation ==
				ResourceSelectScene::ResourceInstallOperation::OnlineDownload &&
			scene.resourceUpdatePromptedByEntry &&
			scene.resourceInstallPrimaryButton != nullptr &&
			scene.resourceInstallPrimaryButton->visible &&
			scene.resourceInstallPrimaryButton->activated &&
			scene.resourceInstallSecondaryButton != nullptr &&
			scene.resourceInstallSecondaryButton->visible &&
			scene.resourceInstallSecondaryButton->activated;
	}

	static bool cancelLocalEntryUpdatePromptWithKeyboard(
		ResourceSelectScene& scene)
	{
		return dispatchResourceKeyboardFrame(scene, KEY_ESCAPE) &&
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Hidden &&
			scene.logicRunning;
	}

	static bool activateLocalEntryUpdateWithKeyboardAcrossFrames(
		ResourceSelectScene& scene)
	{
		if (!dispatchResourceKeyboardFrame(scene, KEY_LEFT) ||
			focusedResourceControl(scene) != "install-primary")
		{
			return false;
		}
		scene.pendingResourceInstall.collectionRoot.clear();
		return dispatchResourceKeyboardFrame(scene, KEY_RETURN) &&
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Failed;
	}

	static bool enterWithoutLocalUpdateWithTouchAcrossFrames(
		ResourceSelectScene& scene)
	{
		return dispatchResourceControlClickAcrossLayout(
				scene, scene.resourceInstallSecondaryButton, true) &&
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Hidden &&
			!scene.logicRunning;
	}

	static bool cancelResourceInstallConfirmation(
		ResourceSelectScene& scene)
	{
		scene.cancelResourceInstall();
		return scene.resourceInstallDialogState ==
			ResourceSelectScene::ResourceInstallDialogState::Hidden;
	}

	static bool rejectMissingStagedResourceActivation(
		ResourceSelectScene& scene)
	{
		scene.resourceInstallDialogState =
			ResourceSelectScene::ResourceInstallDialogState::Downloading;
		scene.resourceInstallWorkerResult =
			std::make_shared<ResourceSelectScene::ResourceInstallWorkerResult>();
		scene.resourceInstallWorkerResult->preparation.status =
			OnlineUpdate::ResourceDownloadPreparationStatus::Success;
		if (scene.pendingResourceInstall.includesCommon)
		{
			scene.resourceInstallWorkerResult->commonPreparation.status =
				OnlineUpdate::ResourceDownloadPreparationStatus::Success;
		}
		scene.resourceInstallWorkerResult->transaction.status =
			OnlineUpdate::ResourceInstallTransactionStatus::Success;
		GameLoading::ExclusiveLoadingCompletion completion;
		completion.taskResult = GameLoading::LoadingTaskResult::success();
		scene.finishResourceInstall(completion);
		return scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Failed &&
			scene.resourceInstallPrimaryButton != nullptr &&
			scene.resourceInstallPrimaryButton->visible &&
			scene.resourceInstallSecondaryButton != nullptr &&
				!scene.resourceInstallSecondaryButton->visible &&
			scene.resourceList != nullptr && !scene.resourceList->visible;
	}

	static bool presentCancelledResourceDownload(
		ResourceSelectScene& scene)
	{
		scene.resourceInstallDialogState =
			ResourceSelectScene::ResourceInstallDialogState::Downloading;
		scene.resourceInstallWorkerResult =
			std::make_shared<ResourceSelectScene::ResourceInstallWorkerResult>();
		scene.resourceInstallWorkerResult->preparation.status =
			OnlineUpdate::ResourceDownloadPreparationStatus::Cancelled;
		GameLoading::ExclusiveLoadingCompletion completion;
		completion.taskResult = GameLoading::LoadingTaskResult::cancellation();
		scene.finishResourceInstall(completion);
		return scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Hidden &&
			scene.resourceInstallPrimaryButton != nullptr &&
			!scene.resourceInstallPrimaryButton->visible &&
			scene.resourceInstallSecondaryButton != nullptr &&
			!scene.resourceInstallSecondaryButton->visible &&
			scene.resourceList != nullptr && scene.resourceList->visible;
	}

	static bool selectResourceEntry(ResourceSelectScene& scene, int index)
	{
		if (scene.resourceList == nullptr || index < 0 ||
			index >= static_cast<int>(scene.resourceEntries.size()))
		{
			return false;
		}
		scene.resourceList->setSelectedIndex(index);
		scene.resourceList->ensureSelectedVisible();
		scene.updateSelectedResourceDetails(index);
		return true;
	}

	static bool resourceCheatHelpVisible(
		const ResourceSelectScene& scene)
	{
		return scene.cheatHelpVisible;
	}

	static bool resourceCheatHelpIsModal(
		const ResourceSelectScene& scene)
	{
		const Rect dialogRect = scene.getCheatHelpDialogRect();
		const Rect closeButtonRect = scene.getCheatHelpCloseButtonRect();
		return scene.cheatHelpVisible
			&& scene.cheatHelpCloseButton != nullptr
			&& scene.cheatHelpCloseButton->visible
			&& scene.cheatHelpCloseButton->activated
			&& scene.resourceList != nullptr
			&& !scene.resourceList->visible
			&& !scene.resourceList->activated
			&& scene.exitButton != nullptr
			&& !scene.exitButton->visible
			&& !scene.exitButton->activated
			&& scene.cheatHelpButton != nullptr
			&& !scene.cheatHelpButton->visible
			&& !scene.cheatHelpButton->activated
			&& dialogRect.x >= 0
			&& dialogRect.y >= 0
			&& dialogRect.x + dialogRect.w <= scene.rect.w
			&& dialogRect.y + dialogRect.h <= scene.rect.h
			&& closeButtonRect.x >= dialogRect.x
			&& closeButtonRect.y >= dialogRect.y
			&& closeButtonRect.x + closeButtonRect.w
				<= dialogRect.x + dialogRect.w
			&& closeButtonRect.y + closeButtonRect.h
				<= dialogRect.y + dialogRect.h;
	}

	static bool resourceExternalFooterRowsDoNotOverlap(
		const ResourceSelectScene& scene)
	{
		if (scene.enableExternalButton == nullptr ||
			scene.externalLinkButtons.empty())
		{
			return false;
		}
		const auto overlaps = [](const Rect& left, const Rect& right)
		{
			return left.x < right.x + right.w &&
				left.x + left.w > right.x &&
				left.y < right.y + right.h &&
				left.y + left.h > right.y;
		};
		const Rect toggle = scene.getEnableExternalButtonRect();
		const Rect path = scene.getExternalResourcePathHintRect();
		const Rect credits = scene.getCreditsTextAreaRect();
		bool linksAreSeparate = true;
		for (int index = 0;
			index < static_cast<int>(scene.externalLinkButtons.size());
			index++)
		{
			const Rect link = scene.getExternalLinkRect(index);
			linksAreSeparate = linksAreSeparate &&
				!overlaps(toggle, link) &&
				!overlaps(path, link) &&
				!overlaps(credits, link);
		}
		return linksAreSeparate && !overlaps(toggle, path) &&
			!overlaps(toggle, credits) &&
			!overlaps(path, credits);
	}

	static bool resourceExternalLinksStayVisible(
		const ResourceSelectScene& scene)
	{
		if (scene.externalLinkButtons.size() != 4)
		{
			return false;
		}
		Rect previous = { 0, 0, 0, 0 };
		for (int index = 0;
			index < static_cast<int>(scene.externalLinkButtons.size());
			index++)
		{
			const auto& button = scene.externalLinkButtons[index];
			const Rect expected = scene.getExternalLinkRect(index);
			if (button == nullptr || !button->visible || !button->activated ||
				button->rect.x != expected.x || button->rect.y != expected.y ||
				button->rect.w != expected.w || button->rect.h != expected.h ||
				expected.w <= 0 || expected.h != 36 ||
				expected.x < scene.startX ||
				expected.x + expected.w > scene.startX + scene.contentWidth ||
				expected.y < scene.panelY ||
				expected.y + expected.h > scene.panelY + scene.panelHeight ||
				(index > 0 && previous.x + previous.w >= expected.x))
			{
				return false;
			}
			previous = expected;
		}
		return true;
	}

	static bool resourceExternalGuidanceIsReadable(
		const ResourceSelectScene& scene)
	{
		if (scene.enableExternalButton == nullptr)
		{
			return false;
		}
		const Rect button = scene.getEnableExternalButtonRect();
		const Rect instructions =
			scene.getExternalResourcePathHintRect();
		const Rect credits = scene.getCreditsTextAreaRect();
		if (scene.compactMobileLayout)
		{
			return button.w <= 220 && button.h == 34 &&
				scene.enableExternalButton->getFontSize() == 20 &&
				instructions.w == 0 && instructions.h == 0 &&
				credits.w == scene.contentWidth && credits.h == 30 &&
				credits.y + credits.h <= button.y;
		}
		return button.w <= 220 && button.h == 34 &&
			scene.enableExternalButton->getFontSize() == 20 &&
			instructions.h == 42 &&
			instructions.y + instructions.h <= button.y;
	}

	static bool resourceExternalDetailsRequireConfirmation(
		ResourceSelectScene& scene)
	{
		if (scene.enableExternalButton == nullptr ||
			scene.externalResourceConfirmButton == nullptr ||
			scene.externalResourceCancelButton == nullptr)
		{
			return false;
		}
		const bool enabledBefore = Config::externalResourcesEnabled;
		if (!dispatchResourceControlClickAcrossLayout(
			scene, scene.enableExternalButton, false))
		{
			return false;
		}
		const Rect dialog = scene.getExternalResourceDialogRect();
		const Rect confirm = scene.getExternalResourceConfirmButtonRect();
		const Rect cancel = scene.getExternalResourceCancelButtonRect();
		const bool opened = scene.externalResourceDialogVisible &&
			scene.externalResourceConfirmButton->visible &&
			scene.externalResourceConfirmButton->activated &&
			scene.externalResourceCancelButton->visible &&
			scene.externalResourceCancelButton->activated &&
			scene.resourceList != nullptr && !scene.resourceList->visible &&
			Config::externalResourcesEnabled == enabledBefore &&
			confirm.x >= dialog.x && confirm.y >= dialog.y &&
			confirm.x + confirm.w <= dialog.x + dialog.w &&
			confirm.y + confirm.h <= dialog.y + dialog.h &&
			cancel.x >= dialog.x && cancel.y >= dialog.y &&
			cancel.x + cancel.w <= dialog.x + dialog.w &&
			cancel.y + cancel.h <= dialog.y + dialog.h;
		scene.hideExternalResourceDialog(false);
		return opened && !scene.externalResourceDialogVisible &&
			scene.resourceList->visible &&
			Config::externalResourcesEnabled == enabledBefore;
	}

	static bool resourceExternalControlsDoNotOverlapContent(
		const ResourceSelectScene& scene)
	{
		if (scene.enableExternalButton == nullptr)
		{
			return false;
		}
		const auto overlaps = [](const Rect& left, const Rect& right)
		{
			return left.x < right.x + right.w &&
				left.x + left.w > right.x &&
				left.y < right.y + right.h &&
				left.y + left.h > right.y;
		};
		const Rect button = scene.getEnableExternalButtonRect();
		const Rect instructions =
			scene.getExternalResourcePathHintRect();
		return !overlaps(button, scene.resourceListArea) &&
			!overlaps(button, scene.detailArea) &&
			!overlaps(instructions, scene.resourceListArea) &&
			!overlaps(instructions, scene.detailArea);
	}

	static bool resourceExternalLayoutIsInsideViewport(
		const ResourceSelectScene& scene, int width, int height)
	{
		const auto isContained = [width, height](const Rect& rect)
		{
			return rect.x >= 0 && rect.y >= 0 &&
				rect.w >= 0 && rect.h >= 0 &&
				rect.x + rect.w <= width &&
				rect.y + rect.h <= height;
		};
		if (scene.enableExternalButton == nullptr ||
			scene.externalLinkButtons.empty())
		{
			return false;
		}
		const Rect panel =
		{
			scene.panelX,
			scene.panelY,
			scene.panelWidth,
			scene.panelHeight
		};
		bool linksAreContained = true;
		for (int index = 0;
			index < static_cast<int>(scene.externalLinkButtons.size());
			index++)
		{
			linksAreContained = linksAreContained &&
				isContained(scene.getExternalLinkRect(index));
		}
		return linksAreContained && isContained(panel) &&
			isContained(scene.resourceListArea) &&
			isContained(scene.detailArea) &&
			isContained(scene.getEnableExternalButtonRect()) &&
			isContained(scene.getExternalResourcePathHintRect()) &&
			isContained(scene.getCreditsTextAreaRect());
	}

	static bool resourceExternalPresentationIsDisabled(
		const ResourceSelectScene& scene)
	{
		return scene.externalResourcePresentationState ==
			ResourceSelectScene::ExternalResourcePresentationState::Disabled;
	}

	static bool resourceExternalPresentationIsWaiting(
		const ResourceSelectScene& scene)
	{
		return scene.externalResourcePresentationState ==
			ResourceSelectScene::ExternalResourcePresentationState::
				WaitingForPermission;
	}

	static bool resourceExternalPresentationRequiresPermission(
		const ResourceSelectScene& scene)
	{
		return scene.externalResourcePresentationState ==
			ResourceSelectScene::ExternalResourcePresentationState::
				PermissionRequired;
	}

	static bool beginExternalPermissionRequest(ResourceSelectScene& scene)
	{
		scene.toggleExternalResources();
		return scene.pendingExternalRescan;
	}

	static std::string externalResourceDirectoryHint(
		const ResourceSelectScene& scene)
	{
		return scene.externalResourceDirectoryPath;
	}

	static bool resourceSceneRunning(
		const ResourceSelectScene& scene)
	{
		return scene.logicRunning;
	}

	static bool resourceCatalogStatusContains(
		const ResourceSelectScene& scene,
		const std::string& text)
	{
		return scene.catalogStatusText.find(text) != std::string::npos;
	}

	static bool dispatchRealResourceCardClick(
		ResourceSelectScene& scene, int cardIndex)
	{
		if (scene.resourceList == nullptr
			|| cardIndex < 0
			|| cardIndex >= static_cast<int>(
				scene.resourceList->cards.size())
			|| scene.resourceList->cards[cardIndex] == nullptr)
		{
			return false;
		}
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		scene.resourceList->setSelectedIndex(cardIndex);
		scene.resourceList->ensureSelectedVisible();
		const auto& card = scene.resourceList->cards[cardIndex];
		const int pointerX = card->rect.x + std::max(1, card->rect.w / 2);
		const int pointerY = card->rect.y + std::max(1, card->rect.h / 2);
		engine->pushEvent(AEvent(
			ET_MOUSEMOTION,
			TOUCH_MOUSEID,
			pointerX,
			pointerY,
			false));
		engine->pushEvent(AEvent(
			ET_MOUSEDOWN,
			MBC_MOUSE_LEFT,
			pointerX,
			pointerY,
			false));
		engine->pushEvent(AEvent(
			ET_MOUSEUP,
			MBC_MOUSE_LEFT,
			pointerX,
			pointerY,
			false));
		scene.allHandleEvents();
		return engine->getEventCount() == 0
			&& scene.resourceList->getSelectedIndex() == cardIndex;
	}

	static bool dispatchResourceControlClick(
		ResourceSelectScene& scene,
		const std::shared_ptr<FlatTextButton>& control)
	{
		if (control == nullptr || !control->visible || !control->activated)
		{
			return false;
		}
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		const int pointerX = control->rect.x + std::max(1, control->rect.w / 2);
		const int pointerY = control->rect.y + std::max(1, control->rect.h / 2);
		engine->pushEvent(AEvent(
			ET_MOUSEMOTION,
			TOUCH_MOUSEID,
			pointerX,
			pointerY,
			false));
		engine->pushEvent(AEvent(
			ET_MOUSEDOWN,
			MBC_MOUSE_LEFT,
			pointerX,
			pointerY,
			false));
		engine->pushEvent(AEvent(
			ET_MOUSEUP,
			MBC_MOUSE_LEFT,
			pointerX,
			pointerY,
			false));
		scene.allHandleEvents();
		return engine->getEventCount() == 0;
	}

	static bool dispatchResourceControlClickAcrossLayout(
		ResourceSelectScene& scene,
		const std::shared_ptr<FlatTextButton>& control,
		bool useTouch)
	{
		if (control == nullptr || !control->visible || !control->activated)
		{
			return false;
		}
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		const int pointerX = control->rect.x + std::max(1, control->rect.w / 2);
		const int pointerY = control->rect.y + std::max(1, control->rect.h / 2);
		const EventTouchID pointerId = useTouch ? 83 : TOUCH_MOUSEID;
		if (!useTouch)
		{
			engine->pushEvent(AEvent(
				ET_MOUSEMOTION,
				TOUCH_MOUSEID,
				pointerX,
				pointerY,
				false));
		}
		engine->pushEvent(AEvent(
			useTouch ? ET_FINGERDOWN : ET_MOUSEDOWN,
			useTouch ? pointerId : MBC_MOUSE_LEFT,
			pointerX,
			pointerY,
			false));
		scene.allHandleEvents();
		if (engine->getEventCount() != 0 ||
			control->touchingDownID != pointerId)
		{
			return false;
		}

		// The production scene recalculates its layout between real pointer-down
		// and pointer-up frames. This catches refreshes that accidentally erase
		// an active button press before the release event arrives.
		scene.updateLayout(scene.rect.w, scene.rect.h);
		if (control->touchingDownID != pointerId)
		{
			return false;
		}
		engine->pushEvent(AEvent(
			useTouch ? ET_FINGERUP : ET_MOUSEUP,
			useTouch ? pointerId : MBC_MOUSE_LEFT,
			pointerX,
			pointerY,
			false));
		scene.allHandleEvents();
		return engine->getEventCount() == 0;
	}

	static bool confirmAutomaticProgramUpdateAcrossKeyboardFrames(
		ResourceSelectScene& scene)
	{
		scene.pendingResourceInstall.collectionRoot.clear();
		if (!dispatchResourceKeyboardFrame(scene, KEY_LEFT) ||
			scene.resourceInstallDialogState !=
				ResourceSelectScene::ResourceInstallDialogState::Confirming ||
			focusedResourceControl(scene) != "install-primary")
		{
			return false;
		}
		return dispatchResourceKeyboardFrame(scene, KEY_RETURN) &&
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Failed;
	}

	static bool confirmAutomaticProgramUpdateWithTouchAcrossFrames(
		ResourceSelectScene& scene)
	{
		scene.pendingResourceInstall.collectionRoot.clear();
		scene.updateLayout(scene.rect.w, scene.rect.h);
		return dispatchResourceControlClickAcrossLayout(
				scene, scene.resourceInstallPrimaryButton, true) &&
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Failed;
	}

	static bool cancelResourceInstallWithTouchAcrossLayout(
		ResourceSelectScene& scene)
	{
		return dispatchResourceControlClickAcrossLayout(
			 scene, scene.resourceInstallSecondaryButton, true) &&
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Hidden;
	}

	static bool startInvalidResourceInstallWithMouseAcrossLayout(
		ResourceSelectScene& scene)
	{
		scene.pendingResourceInstall.collectionRoot.clear();
		return dispatchResourceControlClickAcrossLayout(
			 scene, scene.resourceInstallPrimaryButton, false) &&
			scene.resourceInstallDialogState ==
				ResourceSelectScene::ResourceInstallDialogState::Failed;
	}

	static bool openResourceCheatHelpWithPointer(
		ResourceSelectScene& scene)
	{
		return dispatchResourceControlClick(scene, scene.cheatHelpButton)
			&& scene.cheatHelpVisible;
	}

	static bool closeResourceCheatHelpWithPointer(
		ResourceSelectScene& scene)
	{
		return dispatchResourceControlClick(scene, scene.cheatHelpCloseButton)
			&& !scene.cheatHelpVisible;
	}

	static ResourcePackCardContent resourceCardContent(
		const ResourceSelectScene& scene, int index)
	{
		if (scene.resourceList == nullptr
			|| index < 0
			|| index >= static_cast<int>(
				scene.resourceList->cards.size())
			|| scene.resourceList->cards[index] == nullptr)
		{
			return {};
		}
		return scene.resourceList->cards[index]->content;
	}

	static bool resourceCardShowsOnlineOnlyStatus(
		const ResourceSelectScene& scene, int index)
	{
		if (scene.resourceList == nullptr
			|| index < 0
			|| index >= static_cast<int>(
				scene.resourceList->cards.size())
			|| scene.resourceList->cards[index] == nullptr)
		{
			return false;
		}
		const auto& card = scene.resourceList->cards[index];
		card->updateTextLayout(false, false);
		Rect badgeRect;
		return card->content.onlineOnly
			&& card->content.authorAndVersion.find(u8"未安装")
				!= std::string::npos
			&& card->getOnlineOnlyBadgeRect(badgeRect)
			&& badgeRect.x >= card->rect.x
			&& badgeRect.y >= card->rect.y
			&& badgeRect.x + badgeRect.w
				<= card->rect.x + card->rect.w
			&& badgeRect.y + badgeRect.h
				<= card->rect.y + card->rect.h;
	}

	static bool resourceDescriptionActionGeometryIsContained(
		const ResourceSelectScene& scene, int index, bool expectedVisible)
	{
		if (scene.resourceList == nullptr
			|| index < 0
			|| index >= static_cast<int>(
				scene.resourceList->cards.size())
			|| scene.resourceList->cards[index] == nullptr)
		{
			return false;
		}
		const auto& card = scene.resourceList->cards[index];
		if (card->descriptionButton == nullptr)
		{
			return false;
		}
		if (!expectedVisible)
		{
			return !card->content.showDescriptionAction
				&& !card->descriptionButton->visible
				&& !card->descriptionButton->activated;
		}
		const Rect& buttonRect = card->descriptionButton->rect;
		return card->content.showDescriptionAction
			&& card->descriptionButton->visible
			&& card->descriptionButton->activated
			&& buttonRect.w >= 72
			&& buttonRect.h >= 44
			&& buttonRect.x >= card->rect.x
			&& buttonRect.y >= card->rect.y
			&& buttonRect.x + buttonRect.w
				<= card->rect.x + card->rect.w
			&& buttonRect.y + buttonRect.h
				<= card->rect.y + card->rect.h;
	}

	static bool dispatchResourceDescriptionAction(
		ResourceSelectScene& scene, int cardIndex)
	{
		if (scene.resourceList == nullptr
			|| cardIndex < 0
			|| cardIndex >= static_cast<int>(
				scene.resourceList->cards.size())
			|| scene.resourceList->cards[cardIndex] == nullptr)
		{
			return false;
		}
		const auto& card = scene.resourceList->cards[cardIndex];
		if (card->descriptionButton == nullptr
			|| !card->descriptionButton->visible
			|| !card->descriptionButton->activated)
		{
			return false;
		}
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		ResourceManager& resourceManager = ResourceManager::instance();
		if (!resourceManager.setActiveResourcePack(0))
		{
			return false;
		}
		const std::string activeRootBefore =
			resourceManager.getActiveResourceRoot();
		scene.setRunning(true);
		const Rect buttonRect = card->descriptionButton->rect;
		const int pointerX =
			buttonRect.x + std::max(1, buttonRect.w / 2);
		const int pointerY =
			buttonRect.y + std::max(1, buttonRect.h / 2);
		const EventTouchID touchId = 57;
		engine->pushEvent(AEvent(
			ET_FINGERDOWN, touchId, pointerX, pointerY, false));
		engine->pushEvent(AEvent(
			ET_FINGERUP, touchId, pointerX, pointerY, false));
		scene.allHandleEvents();
		return engine->getEventCount() == 0
			&& scene.logicRunning
			&& resourceManager.getActiveResourceRoot()
				== activeRootBefore
			&& scene.resourceList->getSelectedIndex() == cardIndex
			&& scene.selectedDetails.packIndex == cardIndex
			&& !scene.resourceList->listPointerDown
			&& !card->ownsPointerInteraction(touchId);
	}

	static bool dispatchCanceledResourceDescriptionGestures(
		ResourceSelectScene& scene, int cardIndex)
	{
		if (scene.resourceList == nullptr
			|| cardIndex <= 0
			|| cardIndex >= static_cast<int>(
				scene.resourceList->cards.size())
			|| scene.resourceList->cards[cardIndex] == nullptr)
		{
			return false;
		}
		const auto& card = scene.resourceList->cards[cardIndex];
		if (card->descriptionButton == nullptr
			|| !card->descriptionButton->visible
			|| !card->descriptionButton->activated)
		{
			return false;
		}
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		ResourceManager& resourceManager = ResourceManager::instance();
		if (!resourceManager.setActiveResourcePack(0))
		{
			return false;
		}
		const std::string activeRootBefore =
			resourceManager.getActiveResourceRoot();
		const Rect buttonRect = card->descriptionButton->rect;
		const int buttonX =
			buttonRect.x + std::max(1, buttonRect.w / 2);
		const int buttonY =
			buttonRect.y + std::max(1, buttonRect.h / 2);
		auto resetPreview =
			[&scene]()
			{
				scene.resourceList->setSelectedIndex(0);
				scene.updateSelectedResourceDetails(0);
				scene.setRunning(true);
			};
		auto dispatch =
			[engine, &scene](std::initializer_list<AEvent> events)
			{
				for (AEvent event : events)
				{
					engine->pushEvent(event);
				}
				scene.allHandleEvents();
				return engine->getEventCount() == 0;
			};
		auto previewWasNotCommitted =
			[&scene, &resourceManager, &activeRootBefore]()
			{
				return scene.logicRunning
					&& scene.resourceList->getSelectedIndex() == 0
					&& scene.selectedDetails.packIndex == 0
					&& !scene.resourceList->listPointerDown
					&& resourceManager.getActiveResourceRoot()
						== activeRootBefore;
			};

		resetPreview();
		const EventTouchID releaseOutsideTouch = 58;
		bool ok = dispatch(
		{
			AEvent(ET_FINGERDOWN, releaseOutsideTouch,
				buttonX, buttonY, false),
			AEvent(ET_FINGERUP, releaseOutsideTouch,
				card->rect.x - 8, card->rect.y - 8, false)
		}) && previewWasNotCommitted()
			&& !card->ownsPointerInteraction(releaseOutsideTouch);

		resetPreview();
		const EventTouchID canceledTouch = 59;
		ok = dispatch(
		{
			AEvent(ET_FINGERDOWN, canceledTouch,
				buttonX, buttonY, false),
			AEvent(ET_FINGERCANCEL, canceledTouch,
				buttonX, buttonY, false)
		}) && previewWasNotCommitted()
			&& !card->ownsPointerInteraction(canceledTouch) && ok;

		resetPreview();
		const EventTouchID draggedTouch = 60;
		const int draggedY =
			buttonRect.y + buttonRect.h + 20;
		ok = dispatch(
		{
			AEvent(ET_FINGERDOWN, draggedTouch,
				buttonX, buttonY, false),
			AEvent(ET_FINGERMOTION, draggedTouch,
				buttonX, draggedY, false),
			AEvent(ET_FINGERUP, draggedTouch,
				buttonX, draggedY, false)
		}) && previewWasNotCommitted()
			&& !card->ownsPointerInteraction(draggedTouch) && ok;

		resetPreview();
		const EventTouchID firstTouch = 61;
		const EventTouchID secondTouch = 62;
		const auto& firstCard = scene.resourceList->cards[0];
		const int firstCardX =
			firstCard->rect.x + std::max(1, firstCard->rect.w / 2);
		const int firstCardY =
			firstCard->rect.y + std::max(1, firstCard->rect.h / 2);
		ok = dispatch(
		{
			AEvent(ET_FINGERDOWN, firstTouch,
				firstCardX, firstCardY, false),
			AEvent(ET_FINGERDOWN, secondTouch,
				buttonX, buttonY, false),
			AEvent(ET_FINGERUP, secondTouch,
				buttonX, buttonY, false),
			AEvent(ET_FINGERCANCEL, firstTouch,
				firstCardX, firstCardY, false)
		}) && previewWasNotCommitted()
			&& !firstCard->ownsPointerInteraction(firstTouch)
			&& !card->ownsPointerInteraction(secondTouch) && ok;

		resetPreview();
		const EventTouchID bodyOwnerTouch = 63;
		const EventTouchID competingBodyTouch = 64;
		const int secondCardBodyX = card->rect.x + 8;
		const int secondCardBodyY =
			card->rect.y + std::max(1, card->rect.h / 2);
		ok = dispatch(
		{
			AEvent(ET_FINGERDOWN, bodyOwnerTouch,
				firstCardX, firstCardY, false),
			AEvent(ET_FINGERDOWN, competingBodyTouch,
				secondCardBodyX, secondCardBodyY, false),
			AEvent(ET_FINGERUP, competingBodyTouch,
				secondCardBodyX, secondCardBodyY, false),
			AEvent(ET_FINGERCANCEL, bodyOwnerTouch,
				firstCardX, firstCardY, false)
		}) && previewWasNotCommitted()
			&& !firstCard->ownsPointerInteraction(bodyOwnerTouch)
			&& !card->ownsPointerInteraction(competingBodyTouch)
			&& ok;

		resetPreview();
		const int firstVisibleIndexBeforeDrag =
			scene.resourceList->getFirstVisibleIndex();
		const EventTouchID scrollingTouch = 65;
		const int scrollingY =
			buttonY - ResourcePackList::ItemHeight - 20;
		ok = dispatch(
		{
			AEvent(ET_FINGERDOWN, scrollingTouch,
				buttonX, buttonY, false),
			AEvent(ET_FINGERMOTION, scrollingTouch,
				buttonX, scrollingY, false),
			AEvent(ET_FINGERUP, scrollingTouch,
				buttonX, scrollingY, false)
		}) && scene.logicRunning
			&& resourceManager.getActiveResourceRoot()
				== activeRootBefore
			&& scene.resourceList->getFirstVisibleIndex()
				> firstVisibleIndexBeforeDrag
			&& !scene.resourceList->listPointerDown
			&& !card->ownsPointerInteraction(scrollingTouch)
			&& ok;
		return ok;
	}

	static bool resourceCardSelectionGeometryIsContained(
		int width,
		int height,
		bool expectedSelectionBox)
	{
		ResourcePackCard card;
		card.rect = { 11, 13, width, height };
		card.setContent({ "Resource", "Author    Version", false });
		card.updateTextLayout(false, false);

		Rect selectionBoxRect;
		const bool hasSelectionBox =
			card.getSelectionBoxRect(selectionBoxRect);
		const bool titleIsHorizontallyContained =
			card.titleLabel->rect.x >= card.rect.x
			&& card.titleLabel->rect.w >= 1
			&& card.titleLabel->rect.x + card.titleLabel->rect.w
				<= card.rect.x + card.rect.w;
		const bool selectionBoxIsContained =
			!hasSelectionBox
			|| (selectionBoxRect.x >= card.rect.x
				&& selectionBoxRect.y >= card.rect.y
				&& selectionBoxRect.x + selectionBoxRect.w
					<= card.rect.x + card.rect.w
				&& selectionBoxRect.y + selectionBoxRect.h
					<= card.rect.y + card.rect.h);
		return hasSelectionBox == expectedSelectionBox
			&& titleIsHorizontallyContained
			&& selectionBoxIsContained;
	}

	static bool resourceCardUsesContainedTwoLineLayout(
		int width,
		int height,
		bool expectDescriptionButton,
		bool expectRecentSelectionBadge)
	{
		ResourcePackCard card;
		card.rect = { 11, 13, width, height };
		ResourcePackCardContent content;
		content.title = "Resource";
		content.authorAndVersion = u8"作者：Author    版本：1.0";
		content.showDescriptionAction = true;
		content.wasRecentlySelected = true;
		card.setContent(content);
		card.updateTextLayout(false, false);

		const bool titleIsContained = card.titleLabel->visible
			&& card.titleLabel->rect.x >= card.rect.x
			&& card.titleLabel->rect.y >= card.rect.y
			&& card.titleLabel->rect.x + card.titleLabel->rect.w
				<= card.rect.x + card.rect.w
			&& card.titleLabel->rect.y + card.titleLabel->rect.h
				<= card.rect.y + card.rect.h;
		const bool authorVersionIsContained =
			card.authorVersionLabel->visible
			&& card.authorVersionLabel->rect.x >= card.rect.x
			&& card.authorVersionLabel->rect.y >= card.rect.y
			&& card.authorVersionLabel->rect.x
				+ card.authorVersionLabel->rect.w
				<= card.rect.x + card.rect.w
			&& card.authorVersionLabel->rect.y
				+ card.authorVersionLabel->rect.h
				<= card.rect.y + card.rect.h
			&& card.titleLabel->rect.y + card.titleLabel->rect.h
				<= card.authorVersionLabel->rect.y;
		const bool usesEnlargedFonts =
			card.titleLabel->fontSize == 30
			&& card.authorVersionLabel->fontSize == 20;
		const bool descriptionButtonMatches =
			card.descriptionButton != nullptr
			&& card.descriptionButton->visible == expectDescriptionButton;
		Rect badgeRect;
		const bool hasBadge = card.getRecentSelectionBadgeRect(badgeRect);
		const bool badgeUsesReservedUpperRightLane = !hasBadge
			|| (badgeRect.x >= card.titleLabel->rect.x
					+ card.titleLabel->rect.w
				&& badgeRect.y >= card.rect.y
				&& badgeRect.x + badgeRect.w <= card.rect.x + card.rect.w
				&& badgeRect.y + badgeRect.h
					<= card.authorVersionLabel->rect.y);
		return titleIsContained
			&& authorVersionIsContained
			&& usesEnlargedFonts
			&& descriptionButtonMatches
			&& hasBadge == expectRecentSelectionBadge
			&& badgeUsesReservedUpperRightLane;
	}

	static bool resourceCardOnlineOnlyBadgesAreContained(
		int width,
		bool expectRecentSelectionBadge)
	{
		ResourcePackCard card;
		card.rect = { 11, 13, width, 94 };
		ResourcePackCardContent content;
		content.title = "Online Resource";
		content.authorAndVersion =
			u8"作者：Author    版本：1.0    未安装";
		content.wasRecentlySelected = true;
		content.onlineOnly = true;
		card.setContent(content);
		card.updateTextLayout(false, false);

		Rect onlineOnlyBadgeRect;
		Rect recentSelectionBadgeRect;
		const bool hasOnlineOnlyBadge =
			card.getOnlineOnlyBadgeRect(onlineOnlyBadgeRect);
		const bool hasRecentSelectionBadge =
			card.getRecentSelectionBadgeRect(recentSelectionBadgeRect);
		const bool onlineOnlyBadgeIsContained = hasOnlineOnlyBadge
			&& onlineOnlyBadgeRect.x >= card.titleLabel->rect.x
				+ card.titleLabel->rect.w
			&& onlineOnlyBadgeRect.y >= card.rect.y
			&& onlineOnlyBadgeRect.x + onlineOnlyBadgeRect.w
				<= card.rect.x + card.rect.w
			&& onlineOnlyBadgeRect.y + onlineOnlyBadgeRect.h
				<= card.authorVersionLabel->rect.y;
		const bool badgesDoNotOverlap = !hasRecentSelectionBadge
			|| onlineOnlyBadgeRect.x + onlineOnlyBadgeRect.w
				<= recentSelectionBadgeRect.x;
		const bool recentSelectionBadgeIsContained =
			!hasRecentSelectionBadge
			|| (recentSelectionBadgeRect.x >= card.rect.x
				&& recentSelectionBadgeRect.y >= card.rect.y
				&& recentSelectionBadgeRect.x
					+ recentSelectionBadgeRect.w
					<= card.rect.x + card.rect.w
				&& recentSelectionBadgeRect.y
					+ recentSelectionBadgeRect.h
					<= card.authorVersionLabel->rect.y);
		return onlineOnlyBadgeIsContained
			&& hasRecentSelectionBadge == expectRecentSelectionBadge
			&& badgesDoNotOverlap
			&& recentSelectionBadgeIsContained;
	}

	static int visibleResourceCount(const ResourceSelectScene& scene)
	{
		return scene.resourceList != nullptr
			? scene.resourceList->getVisibleItemCount() : 0;
	}

	static bool compactResourceListRetainsTypography(
		ResourceSelectScene& scene,
		int minimumVisibleCount)
	{
		if (!scene.compactMobileLayout || scene.resourceList == nullptr ||
			scene.resourceList->getVisibleItemCount() < minimumVisibleCount ||
			scene.resourceList->itemHeight != 88 ||
			scene.resourceList->itemGap != 6)
		{
			return false;
		}
		for (auto& card : scene.resourceList->cards)
		{
			if (card == nullptr || !card->visible || card->rect.w <= 0 ||
				card->rect.h <= 0)
			{
				continue;
			}
			card->updateTextLayout(false, false);
			if (card->titleLabel == nullptr ||
				card->authorVersionLabel == nullptr ||
				card->titleLabel->fontSize != 30 ||
				card->authorVersionLabel->fontSize != 20)
			{
				return false;
			}
		}
		return true;
	}

	static int firstVisibleResourceIndex(const ResourceSelectScene& scene)
	{
		return scene.resourceList != nullptr
			? scene.resourceList->getFirstVisibleIndex() : -1;
	}

	static Rect resourceListRect(const ResourceSelectScene& scene)
	{
		return scene.resourceList != nullptr
			? scene.resourceList->rect : Rect{ 0, 0, 0, 0 };
	}

	static Rect resourceScrollbarRect(const ResourceSelectScene& scene)
	{
		return scene.resourceList != nullptr
			? scene.resourceList->scrollbarRect : Rect{ 0, 0, 0, 0 };
	}

	static int expectedResourceScrollbarHeight(
		const ResourceSelectScene& scene)
	{
		return scene.resourceList != nullptr
			? std::max(24,
				scene.resourceList->getVisibleItemCount() *
					scene.resourceList->itemHeight -
					scene.resourceList->itemGap)
			: 0;
	}

	static int presentedSelectedResourceIndex(const ResourceSelectScene& scene)
	{
		if (scene.resourceList == nullptr)
		{
			return -1;
		}
		int selectedIndex = -1;
		for (const auto& card : scene.resourceList->cards)
		{
			if (card == nullptr || !card->isSelected())
			{
				continue;
			}
			if (selectedIndex >= 0)
			{
				return -2;
			}
			selectedIndex = card->index;
		}
		return selectedIndex;
	}

	static int hoveredResourceIndex(const ResourceSelectScene& scene)
	{
		if (scene.resourceList == nullptr)
		{
			return -1;
		}
		for (const auto& card : scene.resourceList->cards)
		{
			if (card != nullptr && card->touchingID == TOUCH_MOUSEID)
			{
				return card->index;
			}
		}
		return -1;
	}

	static bool resourceListFocused(const ResourceSelectScene& scene)
	{
		return scene.resourceList != nullptr && scene.resourceList->isFocused();
	}

	static bool resourceSelectionIndicatorVisible(const ResourceSelectScene& scene)
	{
		return scene.resourceList != nullptr
			&& scene.resourceList->isSelectionIndicatorVisible();
	}

	static std::string focusedResourceControl(const ResourceSelectScene& scene)
	{
		return scene.focusManager.getFocusedNodeId();
	}

	static bool dispatchResourceKeyboard(ResourceSelectScene& scene, int key)
	{
		AEvent event(ET_KEYDOWN, key, 0, 0, false);
		return scene.onHandleEvent(event);
	}

	static bool dispatchResourceKeyboardFrame(
		ResourceSelectScene& scene, int key)
	{
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		engine->pushEvent(AEvent(ET_KEYDOWN, key, 0, 0, false));
		scene.allHandleEvents();
		return engine->getEventCount() == 0;
	}

	static bool dispatchResourceKeyboardFrameWithSyntheticMouseRefresh(
		ResourceSelectScene& scene,
		int key,
		bool pointerInsideFirstVisibleCard)
	{
		if (scene.resourceList == nullptr)
		{
			return false;
		}
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		AEvent keyDown(ET_KEYDOWN, key, 0, 0, false);
		engine->pushEvent(keyDown);
		const Rect& listRect = scene.resourceList->rect;
		Rect pointerTargetRect = listRect;
		const int firstVisibleIndex = scene.resourceList->getFirstVisibleIndex();
		if (pointerInsideFirstVisibleCard
			&& firstVisibleIndex >= 0
			&& firstVisibleIndex
				< static_cast<int>(scene.resourceList->cards.size())
			&& scene.resourceList->cards[firstVisibleIndex] != nullptr)
		{
			pointerTargetRect =
				scene.resourceList->cards[firstVisibleIndex]->rect;
		}
		const int pointerX = pointerInsideFirstVisibleCard
			? pointerTargetRect.x + std::max(1, pointerTargetRect.w / 2)
			: listRect.x - 1;
		const int pointerY = pointerInsideFirstVisibleCard
			? pointerTargetRect.y + std::max(1, pointerTargetRect.h / 2)
			: listRect.y - 1;
		AEvent syntheticMouseRefresh(
			ET_MOUSEMOTION,
			TOUCH_MOUSEID,
			pointerX,
			pointerY,
			false,
			true);
		engine->pushEvent(syntheticMouseRefresh);
		scene.allHandleEvents();
		return engine->getEventCount() == 0;
	}

	static bool dispatchRealResourcePointerFrame(
		ResourceSelectScene& scene, int cardIndex)
	{
		if (scene.resourceList == nullptr
			|| cardIndex < 0
			|| cardIndex >= static_cast<int>(scene.resourceList->cards.size())
			|| scene.resourceList->cards[cardIndex] == nullptr)
		{
			return false;
		}
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		const Rect& cardRect = scene.resourceList->cards[cardIndex]->rect;
		AEvent pointerMotion(
			ET_MOUSEMOTION,
			TOUCH_MOUSEID,
			cardRect.x + std::max(1, cardRect.w / 2),
			cardRect.y + std::max(1, cardRect.h / 2),
			false,
			false);
		engine->pushEvent(pointerMotion);
		scene.allHandleEvents();
		return engine->getEventCount() == 0;
	}

	static bool dispatchResourceListPointer(ResourceSelectScene& scene)
	{
		if (scene.resourceList == nullptr)
		{
			return false;
		}
		AEvent event(
			ET_MOUSEMOTION,
			TOUCH_MOUSEID,
			scene.resourceList->rect.x - 1,
			scene.resourceList->rect.y - 1,
			false);
		scene.resourceList->onHandleEvent(event);
		return true;
	}

	static bool dispatchResourceTouchSelection(
		ResourceSelectScene& scene, int cardIndex)
	{
		if (scene.resourceList == nullptr
			|| cardIndex < 0
			|| cardIndex >= static_cast<int>(
				scene.resourceList->cards.size())
			|| scene.resourceList->cards[cardIndex] == nullptr)
		{
			return false;
		}
		const Rect& cardRect =
			scene.resourceList->cards[cardIndex]->rect;
		const EventTouchID touchId = 27;
		AEvent touchDown(
			ET_FINGERDOWN,
			touchId,
			cardRect.x + std::max(1, cardRect.w / 2),
			cardRect.y + std::max(1, cardRect.h / 2),
			false);
		const bool handled =
			scene.resourceList->onHandleEvent(touchDown);
		scene.resourceList->onPointerInteractionCanceled(touchId);
		return handled;
	}

	static bool dispatchResourceScrollbarGesture(
		ResourceSelectScene& scene)
	{
		if (scene.resourceList == nullptr ||
			scene.resourceList->scrollbar == nullptr ||
			!scene.resourceList->scrollbar->visible)
		{
			return false;
		}
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}

		auto& list = *scene.resourceList;
		const Rect scrollbarRect = list.scrollbar->rect;
		const int pointerX =
			scrollbarRect.x + std::max(1, scrollbarRect.w / 2);
		const int hoverY = scrollbarRect.y + scrollbarRect.h - 2;
		const int selectedBeforeHover = list.selectedIndex;
		engine->pushEvent(AEvent(
			ET_MOUSEMOTION,
			TOUCH_MOUSEID,
			pointerX,
			hoverY,
			false));
		scene.allHandleEvents();
		const bool hoverWasExclusive =
			engine->getEventCount() == 0 &&
			list.selectedIndex == selectedBeforeHover &&
			!list.listPointerDown;

		const Rect thumbRect = list.scrollbar->getThumbRect();
		const int downY = thumbRect.y + std::max(1, thumbRect.h / 2);
		engine->pushEvent(AEvent(
			ET_MOUSEDOWN,
			MBC_MOUSE_LEFT,
			pointerX,
			downY,
			false));
		scene.allHandleEvents();
		const bool downWasExclusive =
			engine->getEventCount() == 0 &&
			list.scrollbarPointerId == TOUCH_MOUSEID &&
			!list.listPointerDown;

		engine->pushEvent(AEvent(
			ET_MOUSEMOTION,
			TOUCH_MOUSEID,
			pointerX,
			hoverY,
			false));
		scene.allHandleEvents();
		const bool dragWasExclusive =
			engine->getEventCount() == 0 &&
			list.scrollbarPointerId == TOUCH_MOUSEID &&
			!list.listPointerDown &&
			list.firstVisibleIndex == list.getMaximumFirstVisibleIndex() &&
			resourceDetailMatchesSelection(scene, list.selectedIndex);

		engine->pushEvent(AEvent(
			ET_MOUSEUP,
			MBC_MOUSE_LEFT,
			pointerX,
			hoverY,
			false));
		scene.allHandleEvents();
		const bool released =
			engine->getEventCount() == 0 &&
			list.scrollbarPointerId == TOUCH_UNTOUCHEDID &&
			!list.listPointerDown;
		return hoverWasExclusive && downWasExclusive &&
			dragWasExclusive && released;
	}

	static bool dispatchResourceConcurrentTouchOwnership(
		ResourceSelectScene& scene)
	{
		if (scene.resourceList == nullptr ||
			scene.resourceList->scrollbar == nullptr ||
			!scene.resourceList->scrollbar->visible ||
			scene.resourceList->cards.empty() ||
			scene.resourceList->cards.front() == nullptr)
		{
			return false;
		}
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		ResourceManager& manager = ResourceManager::instance();
		if (!manager.setActiveResourcePack(0))
		{
			return false;
		}
		const std::string activeRootBefore = manager.getActiveResourceRoot();
		auto dispatch =
			[engine, &scene](
				EventType type,
				EventTouchID pointerID,
				int x,
				int y)
			{
				engine->pushEvent(AEvent(
					type, pointerID, x, y, false));
				scene.allHandleEvents();
				return engine->getEventCount() == 0;
			};
		auto noCardOwns =
			[&scene](EventTouchID pointerID)
			{
				return std::none_of(
					scene.resourceList->cards.cbegin(),
					scene.resourceList->cards.cend(),
					[pointerID](
						const std::shared_ptr<ResourcePackCard>& card)
					{
						return card != nullptr &&
							card->ownsPointerInteraction(pointerID);
					});
			};

		auto& list = *scene.resourceList;
		const Rect scrollbarRect = list.scrollbar->rect;
		const Rect thumbRect = list.scrollbar->getThumbRect();
		const int scrollbarX =
			scrollbarRect.x + std::max(1, scrollbarRect.w / 2);
		const int scrollbarY =
			thumbRect.y + std::max(1, thumbRect.h / 2);
		const EventTouchID firstScrollbarTouch = 41;
		const EventTouchID secondScrollbarTouch = 42;
		bool ok = dispatch(
			ET_FINGERDOWN,
			firstScrollbarTouch,
			scrollbarX,
			scrollbarY);
		ok = dispatch(
			ET_FINGERDOWN,
			secondScrollbarTouch,
			scrollbarX,
			scrollbarY) &&
			list.scrollbarPointerId == firstScrollbarTouch &&
			!list.listPointerDown &&
			noCardOwns(secondScrollbarTouch) && ok;
		ok = dispatch(
			ET_FINGERUP,
			secondScrollbarTouch,
			scrollbarX,
			scrollbarY) &&
			list.scrollbarPointerId == firstScrollbarTouch &&
			manager.getActiveResourceRoot() == activeRootBefore && ok;
		ok = dispatch(
			ET_FINGERUP,
			firstScrollbarTouch,
			scrollbarX,
			scrollbarY) &&
			list.scrollbarPointerId == TOUCH_UNTOUCHEDID &&
			manager.getActiveResourceRoot() == activeRootBefore && ok;

		const Rect cardRect = list.cards.front()->rect;
		const int cardX = cardRect.x + 8;
		const int cardY =
			cardRect.y + std::max(1, cardRect.h / 2);
		const EventTouchID cardTouch = 43;
		const EventTouchID takeoverScrollbarTouch = 44;
		ok = dispatch(
			ET_FINGERDOWN, cardTouch, cardX, cardY) &&
			list.listPointerDown && ok;
		ok = dispatch(
			ET_FINGERDOWN,
			takeoverScrollbarTouch,
			scrollbarX,
			scrollbarY) &&
			list.scrollbarPointerId == takeoverScrollbarTouch &&
			!list.listPointerDown &&
			noCardOwns(cardTouch) && ok;
		ok = dispatch(
			ET_FINGERUP, cardTouch, cardX, cardY) &&
			manager.getActiveResourceRoot() == activeRootBefore && ok;
		ok = dispatch(
			ET_FINGERUP,
			takeoverScrollbarTouch,
			scrollbarX,
			scrollbarY) &&
			list.scrollbarPointerId == TOUCH_UNTOUCHEDID &&
			manager.getActiveResourceRoot() == activeRootBefore && ok;
		return ok;
	}

	static bool dispatchResourceResize(
		ResourceSelectScene& scene, int width, int height)
	{
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		engine->setWindowSize(width, height);
		engine->pushEvent(AEvent(
			ET_WINDOWRESIZE, 0, width, height, false));
		scene.allHandleEvents();
		return engine->getEventCount() == 0;
	}

	static void synchronizeResourceFocusWithInput(ResourceSelectScene& scene)
	{
		scene.synchronizeSemanticFocusWithInput();
	}
};

namespace
{
struct ResourcePackExpectation
{
	const char* id;
	int gameType;
	int saveSlotCount;
	int optionMusicWidth;
	int optionSpeedMaximum;
	int dialogLineCount;
	Rect titleNewGameRect;
};

const ResourcePackExpectation ResourcePacks[] =
{
	{ "JXQY2", GAME_JXQY2, 7, 180, 100, 3, { 284, 227, 274, 36 } },
	{ "XJXQY", GAME_XJXQY, 10, 125, 2, 3, { 204, 164, 199, 51 } },
	{ "YYCS", GAME_YYCS, 7, 194, 2, 3, { 327, 112, 81, 66 } }
};

bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool checkPack(
	bool condition,
	const ResourcePackExpectation& resourcePack,
	const std::string& expectation)
{
	return check(condition, std::string(resourcePack.id) + " " + expectation);
}

void configureGameManager(
	GameManager& gameManager,
	const ResourceManifest& manifest)
{
	gameManager.global.useWav = manifest.useWav;
	gameManager.global.applyResourceManifestFeatures(manifest);
	gameManager.global.loadUiSettings();
	gameManager.goodsManager.configureLayout();
	gameManager.magicManager.configureLayout();
	gameManager.global.data.canInput = true;
}

bool writeSaveMarker(int slot)
{
	const std::string content = "[State]\nReady=1\n";
	return File::writeFileChecked(
		"save/rpg" + std::to_string(slot) + "/game.ini",
		content.data(),
		static_cast<int>(content.size()));
}

std::vector<std::string> makeLongChoiceOptions(int count)
{
	std::vector<std::string> options;
	options.reserve(static_cast<std::size_t>(count));
	for (int index = 0; index < count; index++)
	{
		options.push_back(
			"Controller option " + std::to_string(index)
			+ " uses deliberately long text to exercise production pagination "
				"and stable footer navigation without opening a window.");
	}
	return options;
}

bool nearlyEqual(float left, float right, float tolerance = 0.00001f)
{
	return std::fabs(left - right) <= tolerance;
}

bool testControllerPromptContracts()
{
	bool ok = true;
	Engine* engine = Engine::getInstance();
	engine->setWindowSize(800, 600);
	const ControllerPromptDrawOptions regularOptions =
		ControllerPromptPresenter::bottomBarOptions(engine);
	ok = check(regularOptions.x == 8
		&& regularOptions.y == 540
		&& regularOptions.width == 784
		&& regularOptions.height == 52
		&& regularOptions.fontSize == 14
		&& regularOptions.itemGap == 12,
		"controller prompt bottom bar derives its regular viewport geometry")
		&& ok;

	engine->setWindowSize(640, 360);
	int compactWindowWidth = 0;
	int compactWindowHeight = 0;
	engine->getWindowSize(compactWindowWidth, compactWindowHeight);
	const ControllerPromptDrawOptions compactOptions =
		ControllerPromptPresenter::bottomBarOptions(engine);
	ok = check(compactWindowWidth == 640
		&& compactWindowHeight == 480
		&& compactOptions.x == 8
		&& compactOptions.y == 420
		&& compactOptions.width == 624
		&& compactOptions.height == 52
		&& compactOptions.fontSize == 12
		&& compactOptions.itemGap == 8,
		"controller prompt bottom bar derives its constrained compact viewport geometry")
		&& ok;

	const std::vector<std::string> worldPrompts =
		ControllerPromptPresenter::formatItems(
			ControllerPromptPresenter::worldPromptItems(),
			ControllerPromptLabelTheme{});
	ok = check(worldPrompts == std::vector<std::string>(
		{
			"[左摇杆] 移动",
			"[A] 交谈/互动",
			"[X] 攻击",
			"[RT] 武功一",
			"[Start] 系统"
		}),
		"world prompts expose the primary interaction, attack, skill, and"
		" system actions through catalog-derived Xbox fallback labels") && ok;
	engine->setWindowSize(800, 600);
	return ok;
}

bool testControllerHelpDismissalContract()
{
	bool ok = true;
	ControllerHelpOverlay overlay;
	overlay.setRunning(true);
	AEvent keyUp(ET_KEYUP, KEY_SPACE, 0, 0, false);
	ok = check(GamepadEssentialUITestAccess::dispatchControllerHelpEvent(
		overlay, keyUp)
		&& GamepadEssentialUITestAccess::controllerHelpIsRunning(overlay),
		"controller help occupies key-up without dismissing") && ok;

	AEvent keyDown(ET_KEYDOWN, KEY_SPACE, 0, 0, false);
	ok = check(GamepadEssentialUITestAccess::dispatchControllerHelpEvent(
		overlay, keyDown)
		&& !GamepadEssentialUITestAccess::controllerHelpIsRunning(overlay),
		"controller help dismisses on an arbitrary keyboard press") && ok;

	overlay.setRunning(true);
	ok = check(overlay.handleUIAction(UIAction::Details)
		&& !GamepadEssentialUITestAccess::controllerHelpIsRunning(overlay),
		"controller help dismisses on an arbitrary semantic gamepad action")
		&& ok;

	overlay.setRunning(true);
	AEvent fingerUp(ET_FINGERUP, 7, 320, 240, false);
	ok = check(GamepadEssentialUITestAccess::dispatchControllerHelpEvent(
		overlay, fingerUp)
		&& GamepadEssentialUITestAccess::controllerHelpIsRunning(overlay),
		"controller help occupies touch release without dismissing") && ok;

	AEvent fingerDown(ET_FINGERDOWN, 7, 320, 240, false);
	ok = check(GamepadEssentialUITestAccess::dispatchControllerHelpEvent(
		overlay, fingerDown)
		&& !GamepadEssentialUITestAccess::controllerHelpIsRunning(overlay),
		"controller help dismisses when the screen is touched") && ok;

	GamepadEssentialUITestAccess::resizeElementTree(overlay, 960, 540);
	ok = check(overlay.rect.x == 0 && overlay.rect.y == 0
		&& overlay.rect.w == 960 && overlay.rect.h == 540,
		"controller help remains a full-window modal after resize") && ok;
	return ok;
}

float expectedScrollbarVolume(const Scrollbar& scrollbar)
{
	if (scrollbar.max <= scrollbar.min)
	{
		return 0.0f;
	}
	return static_cast<float>(scrollbar.position - scrollbar.min)
		/ static_cast<float>(scrollbar.max - scrollbar.min);
}

int expectedDefaultSpeedPosition(const Scrollbar& scrollbar)
{
	if (scrollbar.max <= scrollbar.min)
	{
		return scrollbar.min;
	}
	return static_cast<int>(std::round(
		(SPEED_TIME_DEFAULT - SPEED_TIME_MIN)
			/ (SPEED_TIME_MAX - SPEED_TIME_MIN)
			* static_cast<float>(scrollbar.max - scrollbar.min)
			+ static_cast<float>(scrollbar.min)));
}

float expectedSpeedAtPosition(const Scrollbar& scrollbar)
{
	if (scrollbar.max <= scrollbar.min)
	{
		return SPEED_TIME_DEFAULT;
	}
	if (scrollbar.position <= scrollbar.min)
	{
		return SPEED_TIME_MIN;
	}
	if (scrollbar.position >= scrollbar.max)
	{
		return SPEED_TIME_MAX;
	}
	return static_cast<float>(scrollbar.position - scrollbar.min)
		/ static_cast<float>(scrollbar.max - scrollbar.min)
		* (SPEED_TIME_MAX - SPEED_TIME_MIN) + SPEED_TIME_MIN;
}

bool testResourceSelectionController(
	ResourceManager& resourceManager)
{
	bool ok = true;
	const std::string previousAssetsCollectionRoot =
		File::getAssetsCollectionRoot();
	const auto& packs = resourceManager.getDiscoveredPacks();
	if (!check(packs.size() >= 3,
		"resource selection discovers the production resource packs"))
	{
		return false;
	}
	ResourceSelectScene configurationErrorScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		configurationErrorScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::presentResourceConfigurationError(
			configurationErrorScene) &&
			GamepadEssentialUITestAccess::configurationErrorCannotEnter(
				configurationErrorScene),
		"resource configuration errors remain visible but cannot be entered")
		&& ok;
#if defined(__ANDROID__) || \
	defined(JXQY_TEST_ANDROID_EXTERNAL_RESOURCE_UI)
	const bool originalExternalResourcesEnabled =
		Config::externalResourcesEnabled;
	Config::externalResourcesEnabled = true;
	ResourceSelectScene revokedPermissionScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		revokedPermissionScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::
			resourceExternalPresentationRequiresPermission(
				revokedPermissionScene),
		"persisted external-resource preference is never presented as enabled"
		" when Android all-files access is absent") && ok;
	Config::externalResourcesEnabled = false;
#endif

	ok = check(
		GamepadEssentialUITestAccess::
			resourceCardSelectionGeometryIsContained(280, 94, true)
			&& GamepadEssentialUITestAccess::
				resourceCardSelectionGeometryIsContained(82, 94, false)
			&& GamepadEssentialUITestAccess::
				resourceCardSelectionGeometryIsContained(83, 28, true)
			&& GamepadEssentialUITestAccess::
				resourceCardSelectionGeometryIsContained(83, 27, false)
			&& GamepadEssentialUITestAccess::
				resourceCardSelectionGeometryIsContained(1, 1, false),
		"resource card selection box and text lane stay inside normal and"
		" degenerate card rectangles") && ok;
	ok = check(
		GamepadEssentialUITestAccess::
			resourceCardUsesContainedTwoLineLayout(
				280, 94, true, true)
			&& GamepadEssentialUITestAccess::
				resourceCardUsesContainedTwoLineLayout(
					150, 94, false, false)
			&& GamepadEssentialUITestAccess::
				resourceCardUsesContainedTwoLineLayout(
					280, 76, true, true),
		"resource cards use enlarged title and author-version lines without"
		" crossing the card or mobile description-button lanes") && ok;
	ok = check(
		GamepadEssentialUITestAccess::
			resourceCardOnlineOnlyBadgesAreContained(280, true)
			&& GamepadEssentialUITestAccess::
				resourceCardOnlineOnlyBadgesAreContained(200, false),
		"online-only resource cards reserve a contained not-downloaded badge"
		" and prioritize it when the recent-selection badge does not fit")
		&& ok;
	ok = check(
		GamepadEssentialUITestAccess::localResourcesUsePreferredDisplayOrder()
			&& GamepadEssentialUITestAccess::
			onlineOnlyResourcesUsePreferredDisplayOrder(),
		"resource lists keep the recent local selection first, then order local"
		" and online-only entries as JXQY2, YYCS, XJXQY, and other mods")
		&& ok;

	ResourceSelectScene navigationScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		navigationScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::
			resourceHeaderActionsAreLarge(navigationScene),
		"resource selection keeps all enlarged header actions contained")
		&& ok;
	ResourceSelectScene displaySettingsScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		displaySettingsScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::displaySettingsPageIsUsable(
			displaySettingsScene),
		"desktop resource selection opens a dedicated display-settings page"
		" with bounded resolutions and restores the resource list on return")
		&& ok;
	ResourceSelectScene removalScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		removalScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::
			resourceRemovalRequiresExplicitSaveChoice(removalScene),
		"resource removal has no default save policy and enables deletion only"
		" after an explicit choice") && ok;
	ResourceSelectScene meteredDownloadScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		meteredDownloadScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::
			meteredDownloadRequiresSecondConfirmation(meteredDownloadScene),
		"metered downloads require a second confirmation and allow returning"
		" to the package summary") && ok;
	ResourceSelectScene compactActionScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		compactActionScene, 400, 480);
	ok = check(
		GamepadEssentialUITestAccess::
			resourceHeaderActionsAreLarge(compactActionScene),
		"resource selection keeps enlarged actions inside the compact"
		" header") && ok;
	ok = check(
		GamepadEssentialUITestAccess::resourceExternalLinksStayVisible(
			navigationScene)
			&& GamepadEssentialUITestAccess::resourceExternalLinksStayVisible(
				compactActionScene),
		"resource selection keeps all four external links visible and aligned"
		" with the header action height") && ok;
#if defined(_WIN32) || defined(__linux__)
	ResourceSelectScene programUpdateScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		programUpdateScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::presentProgramUpdateAction(
			programUpdateScene) &&
		GamepadEssentialUITestAccess::programUpdateActionIsContained(
			programUpdateScene) &&
		programUpdateScene.handleUIAction(UIAction::NavigateUp) &&
		GamepadEssentialUITestAccess::focusedResourceControl(
			programUpdateScene) == "program-action" &&
		GamepadEssentialUITestAccess::activateProgramUpdate(
			programUpdateScene),
		"the unified check action stays available while a separate contained"
		" program action opens the update confirmation") && ok;
	ResourceSelectScene programPointerScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		programPointerScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::presentProgramUpdateAction(
			programPointerScene) &&
		GamepadEssentialUITestAccess::activateProgramUpdateWithPointer(
			programPointerScene) &&
		GamepadEssentialUITestAccess::
			startInvalidResourceInstallWithMouseAcrossLayout(
				programPointerScene),
		"the separate contained program action opens the update confirmation"
		" and its download button accepts a real pointer click across layout")
		&& ok;
	ResourceSelectScene modalProgramActionScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		modalProgramActionScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::
			programActionStaysHiddenDuringModalRefresh(modalProgramActionScene),
		"program actions stay hidden when catalog-driven controls refresh behind"
		" a modal dialog and return after dismissal") && ok;
	ResourceSelectScene currentProgramScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		currentProgramScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::presentCurrentOnlineProgramDownload(
			currentProgramScene),
		"the program action downloads the sole online package at the current"
		" Version") && ok;
	ResourceSelectScene automaticProgramKeyboardScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		automaticProgramKeyboardScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::completeAutomaticProgramUpdateCheck(
			automaticProgramKeyboardScene, "9.9.0-test", true) &&
		GamepadEssentialUITestAccess::
			confirmAutomaticProgramUpdateAcrossKeyboardFrames(
				automaticProgramKeyboardScene),
		"a newer program version opens a separate confirmation while retaining"
		" the manual action, and keyboard selection and activation work in"
		" separate engine frames") && ok;
	ResourceSelectScene automaticProgramTouchScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		automaticProgramTouchScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::completeAutomaticProgramUpdateCheck(
			automaticProgramTouchScene, "9.9.0-test", true) &&
		GamepadEssentialUITestAccess::
			confirmAutomaticProgramUpdateWithTouchAcrossFrames(
				automaticProgramTouchScene),
		"the automatic program confirmation accepts touch down and touch up in"
		" separate engine frames with a layout pass between them") && ok;
	ResourceSelectScene currentAutomaticProgramScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		currentAutomaticProgramScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::completeAutomaticProgramUpdateCheck(
			currentAutomaticProgramScene,
			JxqyBuildVersion::EngineVersion,
			false),
		"an equal online program version keeps the manual reinstall action but"
		" does not open the automatic update confirmation") && ok;
#endif
#if defined(__ANDROID__) || \
	defined(JXQY_TEST_ANDROID_EXTERNAL_RESOURCE_UI)
	ResourceSelectScene shortActionScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		shortActionScene, 400, 360);
	ResourceSelectScene shortLandscapeScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		shortLandscapeScene, 800, 360);
	ok = check(
		GamepadEssentialUITestAccess::
			resourceExternalFooterRowsDoNotOverlap(navigationScene)
			&& GamepadEssentialUITestAccess::
				resourceExternalFooterRowsDoNotOverlap(compactActionScene)
			&& GamepadEssentialUITestAccess::
				resourceExternalFooterRowsDoNotOverlap(shortActionScene)
			&& GamepadEssentialUITestAccess::
				resourceExternalLayoutIsInsideViewport(
					navigationScene, 800, 480)
			&& GamepadEssentialUITestAccess::
				resourceExternalLayoutIsInsideViewport(
					compactActionScene, 400, 480)
			&& GamepadEssentialUITestAccess::
				resourceExternalLayoutIsInsideViewport(
					shortActionScene, 400, 360),
		"compact mobile resource selection keeps the external-resource action"
		" and all four external links contained in separate footer rows") && ok;
	ok = check(
		GamepadEssentialUITestAccess::resourceExternalLinksStayVisible(
			shortActionScene)
			&& GamepadEssentialUITestAccess::resourceExternalLinksStayVisible(
				shortLandscapeScene),
		"compact mobile resource selection keeps all four external links"
		" visible at short viewport sizes") && ok;
	ok = check(
		GamepadEssentialUITestAccess::
			resourceExternalGuidanceIsReadable(navigationScene)
			&& GamepadEssentialUITestAccess::
				resourceExternalGuidanceIsReadable(compactActionScene)
			&& GamepadEssentialUITestAccess::
				resourceExternalControlsDoNotOverlapContent(
					navigationScene)
			&& GamepadEssentialUITestAccess::
				resourceExternalControlsDoNotOverlapContent(
					compactActionScene)
			&& GamepadEssentialUITestAccess::
				resourceExternalControlsDoNotOverlapContent(
					shortActionScene),
		"mobile external-resource action remains readable and does not overlap"
		" the resource list or detail panel") && ok;
	ok = check(
		GamepadEssentialUITestAccess::compactResourceListRetainsTypography(
			navigationScene, 2)
			&& GamepadEssentialUITestAccess::
				compactResourceListRetainsTypography(
					shortLandscapeScene, 1),
		"compact 800x480 and 800x360 mobile layouts keep resource"
		" cards while retaining the 30px title and 20px author-version fonts")
		&& ok;
	ok = check(
		GamepadEssentialUITestAccess::
			resourceExternalPresentationIsDisabled(navigationScene)
			&& GamepadEssentialUITestAccess::
				externalResourceDirectoryHint(navigationScene) ==
					"/storage/emulated/0/Download/jxqy/assets/",
		"mobile external resources start disabled and present the fixed device"
		" directory even when the host surrogate has no Android storage root")
		&& ok;
	ok = check(
		GamepadEssentialUITestAccess::
			resourceExternalDetailsRequireConfirmation(navigationScene),
		"the mobile external-resource button opens a modal explanation and"
		" closing it does not change the setting without confirmation") && ok;
	ok = check(
		GamepadEssentialUITestAccess::
			beginExternalPermissionRequest(navigationScene)
			&& !Config::externalResourcesEnabled
			&& GamepadEssentialUITestAccess::
				resourceExternalPresentationIsWaiting(navigationScene),
		"requesting external-resource permission does not present or persist"
		" the feature as enabled before permission is granted") && ok;
#endif
	const ResourcePackCardContent firstCard =
		GamepadEssentialUITestAccess::resourceCardContent(
			navigationScene, 0);
	const std::string expectedFirstVersion =
		packs[0].manifest.releaseMetadata.displayVersion.empty()
			? std::string(u8"未声明")
			: packs[0].manifest.releaseMetadata.displayVersion;
	const std::string expectedFirstAuthor =
		packs[0].getDisplayAuthorText().empty()
			? std::string(u8"作者：未声明")
			: packs[0].getDisplayAuthorText();
	const std::string expectedFirstTitle = packs[0].manifest.name.empty()
		? std::string(u8"未命名资源") : packs[0].manifest.name;
#ifdef __MOBILE__
	const bool expectedDescriptionAction = true;
#else
	const bool expectedDescriptionAction = false;
#endif
	ok = check(firstCard.title == expectedFirstTitle
			&& firstCard.authorAndVersion
				== expectedFirstAuthor + u8"    版本："
					+ expectedFirstVersion
			&& firstCard.showDescriptionAction
				== expectedDescriptionAction
			&& firstCard.wasRecentlySelected
				== packs[0].wasRecentlySelected
			&& !firstCard.onlineOnly,
		"resource cards expose only title and author-version text plus the"
		" mobile-only description action and applicable status markers") && ok;
	ok = check(
		GamepadEssentialUITestAccess::selectedResourceDetailIndex(
			navigationScene) == 0
			&& GamepadEssentialUITestAccess::
				selectedResourceDetailsContainSecondaryMetadata(
					navigationScene, packs[0])
			&& !GamepadEssentialUITestAccess::selectedResourceDescription(
				navigationScene).empty()
			&& (packs[0].manifest.releaseMetadata.
					descriptionFilePath.empty()
				|| GamepadEssentialUITestAccess::
					selectedResourceDescriptionLoadedFromPack(
						navigationScene))
			&& (!packs[0].manifest.releaseMetadata.coverPath.empty()
				|| GamepadEssentialUITestAccess::
					selectedResourceUsesCoverPlaceholder(navigationScene)),
		"resource selection moves ID, release, compatibility, and recent-use"
		" metadata into matching details, reads a declared description, and"
		" uses one cover placeholder when no cover is declared") && ok;
#ifdef __MOBILE__
	ResourceSelectScene descriptionActionScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		descriptionActionScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::
			resourceDescriptionActionGeometryIsContained(
				descriptionActionScene, 1, true)
			&& GamepadEssentialUITestAccess::
				dispatchResourceDescriptionAction(
					descriptionActionScene, 1)
			&& GamepadEssentialUITestAccess::
				dispatchCanceledResourceDescriptionGestures(
					descriptionActionScene, 1),
		"mobile description action keeps a 72x44 in-card hit target, previews"
		" only on a completed tap, and ignores release-outside, cancel, and"
		" drag or a second pointer without activating a MOD") && ok;
#else
	ok = check(
		GamepadEssentialUITestAccess::
			resourceDescriptionActionGeometryIsContained(
				navigationScene, 0, false),
		"desktop resource cards do not add the mobile description action") && ok;
#endif
	ResourceSelectScene scrollbarScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		scrollbarScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::dispatchResourceScrollbarGesture(
			scrollbarScene),
		"resource scrollbar hover and drag own one exclusive pointer"
		" transaction while keeping selection details coherent") && ok;
	ResourceSelectScene concurrentTouchScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		concurrentTouchScene, 800, 480);
	ok = check(
		GamepadEssentialUITestAccess::
			dispatchResourceConcurrentTouchOwnership(
				concurrentTouchScene),
		"resource scrollbar ownership rejects a second touch and cancels"
		" an earlier card press without confirming a pack") && ok;
	ok = check(!GamepadEssentialUITestAccess::resourceListFocused(navigationScene)
		&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(
			navigationScene)
		&& GamepadEssentialUITestAccess::presentedSelectedResourceIndex(
			navigationScene) == 0
		&& GamepadEssentialUITestAccess::focusedResourceControl(navigationScene)
			== "resource-list"
		&& GamepadEssentialUITestAccess::selectedResourceIndex(navigationScene) == 0,
		"resource selection presents its default candidate without claiming"
		" keyboard or gamepad focus") && ok;
	ok = check(GamepadEssentialUITestAccess::dispatchResourceKeyboard(
		navigationScene, KEY_DOWN)
		&& GamepadEssentialUITestAccess::resourceListFocused(navigationScene)
		&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(
			navigationScene)
		&& GamepadEssentialUITestAccess::selectedResourceIndex(navigationScene) == 1
		&& GamepadEssentialUITestAccess::resourceDetailMatchesSelection(
			navigationScene, 1),
		"resource selection restores keyboard focus, performs the first item"
		" step, and refreshes details") && ok;

	Engine* engine = Engine::getInstance();
	int previousWindowWidth = 0;
	int previousWindowHeight = 0;
	engine->getWindowSize(previousWindowWidth, previousWindowHeight);
	engine->setWindowSize(1280, 720);
	for (const bool pointerInsideFirstVisibleCard : { false, true })
	{
		ResourceSelectScene frameScene;
		ok = check(
			GamepadEssentialUITestAccess::prepareProductionResourceSelection(
				frameScene) &&
			GamepadEssentialUITestAccess::automaticCatalogCheckStarted(
				frameScene),
			"resource selection production initialization starts one automatic"
			" catalog check and creates the keyboard synthetic-refresh fixture")
			&& ok;
		ok = check(
			GamepadEssentialUITestAccess::
				dispatchResourceKeyboardFrameWithSyntheticMouseRefresh(
					frameScene,
					KEY_DOWN,
					pointerInsideFirstVisibleCard)
				&& GamepadEssentialUITestAccess::selectedResourceIndex(
					frameScene) == 1
				&& GamepadEssentialUITestAccess::firstVisibleResourceIndex(
					frameScene) == 0
				&& GamepadEssentialUITestAccess::resourceListFocused(frameScene)
				&& GamepadEssentialUITestAccess::
					resourceSelectionIndicatorVisible(frameScene)
				&& GamepadEssentialUITestAccess::
					presentedSelectedResourceIndex(frameScene) == 1
				&& GamepadEssentialUITestAccess::
					resourceHeaderActionsAreLarge(frameScene),
			std::string(
				"resource selection keeps a single keyboard step and its"
				" concrete selected card across the engine synthetic mouse"
				" refresh when the pointer is ")
				+ (pointerInsideFirstVisibleCard
					? "inside the list"
					: "outside the list")) && ok;
	}

	ResourceSelectScene realPointerScene;
	const std::string activeRootBeforePointer =
		resourceManager.getActiveResourceRoot();
	ok = check(
		GamepadEssentialUITestAccess::prepareProductionResourceSelection(
			realPointerScene)
			&& GamepadEssentialUITestAccess::dispatchRealResourcePointerFrame(
				realPointerScene, 1)
			&& GamepadEssentialUITestAccess::selectedResourceIndex(
				realPointerScene) == 1
			&& GamepadEssentialUITestAccess::resourceDetailMatchesSelection(
				realPointerScene, 1)
			&& GamepadEssentialUITestAccess::hoveredResourceIndex(
				realPointerScene) == 1
			&& !GamepadEssentialUITestAccess::resourceListFocused(
				realPointerScene)
			&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(
				realPointerScene)
			&& GamepadEssentialUITestAccess::presentedSelectedResourceIndex(
				realPointerScene) == 1
			&& resourceManager.getActiveResourceRoot()
				== activeRootBeforePointer,
		"resource selection keeps real mouse takeover, refreshes card details,"
		" presents the current candidate, and does not activate it")
		&& ok;

	ResourceSelectScene touchScene;
	const std::string activeRootBeforeTouch =
		resourceManager.getActiveResourceRoot();
	ok = check(
		GamepadEssentialUITestAccess::prepareProductionResourceSelection(
			touchScene)
			&& GamepadEssentialUITestAccess::dispatchResourceTouchSelection(
				touchScene, 1)
			&& GamepadEssentialUITestAccess::selectedResourceIndex(
				touchScene) == 1
			&& GamepadEssentialUITestAccess::resourceDetailMatchesSelection(
				touchScene, 1)
			&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(
				touchScene)
			&& GamepadEssentialUITestAccess::presentedSelectedResourceIndex(
				touchScene) == 1
			&& resourceManager.getActiveResourceRoot()
				== activeRootBeforeTouch,
		"resource selection presents the touch candidate and refreshes its"
		" details without activating it") && ok;

	engine->setWindowSize(previousWindowWidth, previousWindowHeight);

	ResourceSelectScene resizeScene;
	engine->setWindowSize(800, 720);
	GamepadEssentialUITestAccess::prepareResourceSelection(
		resizeScene, 800, 720);
	const int tallVisibleCount =
		GamepadEssentialUITestAccess::visibleResourceCount(resizeScene);
	const int resizeTargetIndex = std::min(
		static_cast<int>(packs.size()) - 1,
		std::max(1, tallVisibleCount - 1));
	for (int index = 0; index < resizeTargetIndex; index++)
	{
		resizeScene.handleUIAction(UIAction::NavigateDown);
	}
	const Rect tallListRect =
		GamepadEssentialUITestAccess::resourceListRect(resizeScene);
	const Rect tallDetailRect =
		GamepadEssentialUITestAccess::resourceDetailRect(resizeScene);
	const Rect tallCoverRect =
		GamepadEssentialUITestAccess::resourceCoverRect(resizeScene);
	ok = check(tallVisibleCount > 1
			&& GamepadEssentialUITestAccess::resourceDetailsUseWideLayout(
				resizeScene)
			&& tallDetailRect.x >= tallListRect.x + tallListRect.w
			&& tallCoverRect.x >= tallDetailRect.x
			&& tallCoverRect.y >= tallDetailRect.y
			&& tallCoverRect.x + tallCoverRect.w
				<= tallDetailRect.x + tallDetailRect.w
			&& tallCoverRect.y + tallCoverRect.h
				<= tallDetailRect.y + tallDetailRect.h
			&& GamepadEssentialUITestAccess::selectedResourceIndex(
				resizeScene) == resizeTargetIndex
			&& GamepadEssentialUITestAccess::focusedResourceControl(
				resizeScene) == "resource-list",
		"resource selection resize fixture reaches a non-default visible card")
		&& ok;
	ok = check(
		GamepadEssentialUITestAccess::dispatchResourceResize(
			resizeScene, 719, 720)
			&& GamepadEssentialUITestAccess::resourceDetailsUseWideLayout(
				resizeScene),
		"resource selection keeps the wide layout immediately below the"
		" responsive margin transition") && ok;
	const Rect beforeMarginTransitionListRect =
		GamepadEssentialUITestAccess::resourceListRect(resizeScene);
	ok = check(
		GamepadEssentialUITestAccess::dispatchResourceResize(
			resizeScene, 720, 720)
			&& GamepadEssentialUITestAccess::resourceDetailsUseWideLayout(
				resizeScene)
			&& GamepadEssentialUITestAccess::resourceListRect(resizeScene).w
				>= beforeMarginTransitionListRect.w,
		"resource selection width increase does not regress to a narrower"
		" layout at the margin transition") && ok;
	ok = check(
		GamepadEssentialUITestAccess::dispatchResourceResize(
			resizeScene, 640, 480),
		"resource selection consumes a production resize event") && ok;
	const Rect compactListRect =
		GamepadEssentialUITestAccess::resourceListRect(resizeScene);
	const Rect compactDetailRect =
		GamepadEssentialUITestAccess::resourceDetailRect(resizeScene);
	const Rect compactScrollbarRect =
		GamepadEssentialUITestAccess::resourceScrollbarRect(resizeScene);
	const int compactVisibleCount =
		GamepadEssentialUITestAccess::visibleResourceCount(resizeScene);
	const int compactFirstVisibleIndex =
		GamepadEssentialUITestAccess::firstVisibleResourceIndex(resizeScene);
	ok = check(compactListRect.x != tallListRect.x
			|| compactListRect.y != tallListRect.y
			|| compactListRect.w != tallListRect.w
			|| compactListRect.h != tallListRect.h,
		"resource selection resize event does not leave the old list geometry")
		&& ok;
	ok = check(compactVisibleCount > 0
			&& compactVisibleCount < tallVisibleCount
			&& !GamepadEssentialUITestAccess::resourceDetailsUseWideLayout(
				resizeScene)
			&& compactDetailRect.y + compactDetailRect.h
				<= compactListRect.y
			&& compactScrollbarRect.y == compactListRect.y
			&& compactScrollbarRect.y + compactScrollbarRect.h
				<= compactListRect.y + compactListRect.h
			&& compactScrollbarRect.h ==
				GamepadEssentialUITestAccess::
					expectedResourceScrollbarHeight(resizeScene)
			&& GamepadEssentialUITestAccess::selectedResourceIndex(
				resizeScene) == resizeTargetIndex
			&& compactFirstVisibleIndex <= resizeTargetIndex
			&& resizeTargetIndex
				< compactFirstVisibleIndex + compactVisibleCount
			&& GamepadEssentialUITestAccess::focusedResourceControl(
				resizeScene) == "resource-list"
			&& GamepadEssentialUITestAccess::
				resourceHeaderActionsAreLarge(resizeScene),
		"resource selection resize preserves its selected card and keeps it"
		" inside the compact visible range") && ok;

	ok = check(
		GamepadEssentialUITestAccess::dispatchResourceResize(
			resizeScene, 1100, 500),
		"resource selection consumes the 1100x500 landscape resize") && ok;
	const Rect landscapeListRect =
		GamepadEssentialUITestAccess::resourceListRect(resizeScene);
	const Rect landscapeDetailRect =
		GamepadEssentialUITestAccess::resourceDetailRect(resizeScene);
	const Rect landscapeCoverRect =
		GamepadEssentialUITestAccess::resourceCoverRect(resizeScene);
	ok = check(
		GamepadEssentialUITestAccess::resourceDetailsUseWideLayout(
			resizeScene)
			&& landscapeListRect.w > 0
			&& landscapeListRect.h > 0
			&& landscapeDetailRect.w > 0
			&& landscapeDetailRect.h > 0
			&& landscapeDetailRect.x
				>= landscapeListRect.x + landscapeListRect.w
			&& landscapeCoverRect.x >= landscapeDetailRect.x
			&& landscapeCoverRect.y >= landscapeDetailRect.y
			&& landscapeCoverRect.x + landscapeCoverRect.w
				<= landscapeDetailRect.x + landscapeDetailRect.w
			&& landscapeCoverRect.y + landscapeCoverRect.h
				<= landscapeDetailRect.y + landscapeDetailRect.h,
		"resource selection keeps list, details, and cover contained in the"
		" 1100x500 landscape layout") && ok;

	ok = check(
		GamepadEssentialUITestAccess::dispatchResourceResize(
			resizeScene, 400, 480),
		"resource selection consumes the narrow-screen resize") && ok;
	const Rect narrowListRect =
		GamepadEssentialUITestAccess::resourceListRect(resizeScene);
	const Rect narrowDetailRect =
		GamepadEssentialUITestAccess::resourceDetailRect(resizeScene);
	const Rect narrowCoverRect =
		GamepadEssentialUITestAccess::resourceCoverRect(resizeScene);
	ok = check(
		!GamepadEssentialUITestAccess::resourceDetailsUseWideLayout(
			resizeScene)
			&& narrowListRect.w > 0
			&& narrowListRect.h > 0
			&& narrowDetailRect.w > 0
			&& narrowDetailRect.h > 0
			&& narrowDetailRect.y + narrowDetailRect.h
				<= narrowListRect.y
			&& narrowCoverRect.x >= narrowDetailRect.x
			&& narrowCoverRect.y >= narrowDetailRect.y
			&& narrowCoverRect.x + narrowCoverRect.w
				<= narrowDetailRect.x + narrowDetailRect.w
			&& narrowCoverRect.y + narrowCoverRect.h
				<= narrowDetailRect.y + narrowDetailRect.h
			&& GamepadEssentialUITestAccess::
				resourceDescriptionActionGeometryIsContained(
					resizeScene, resizeTargetIndex,
					expectedDescriptionAction),
		"resource selection keeps positive contained detail geometry and the"
		" optional description action on a narrow screen") && ok;

	ok = check(
			GamepadEssentialUITestAccess::navigateResourceSelectionToUpdate(
				resizeScene)
			&& resizeScene.handleUIAction(UIAction::NavigateRight)
			&& GamepadEssentialUITestAccess::focusedResourceControl(
				resizeScene) == "exit",
		"resource selection resize fixture reaches update and exit actions")
		&& ok;
	const int firstVisibleBeforeExpandedResize =
		GamepadEssentialUITestAccess::firstVisibleResourceIndex(resizeScene);
	ok = check(
		GamepadEssentialUITestAccess::dispatchResourceResize(
			resizeScene, 1024, 720)
			&& GamepadEssentialUITestAccess::focusedResourceControl(
				resizeScene) == "exit"
			&& GamepadEssentialUITestAccess::selectedResourceIndex(
				resizeScene) == resizeTargetIndex
			&& GamepadEssentialUITestAccess::firstVisibleResourceIndex(
				resizeScene) == firstVisibleBeforeExpandedResize
			&& GamepadEssentialUITestAccess::
				resourceHeaderActionsAreLarge(resizeScene),
		"resource selection expanded resize preserves exit focus, selected"
		" card, and the existing valid list range") && ok;
	engine->setWindowSize(previousWindowWidth, previousWindowHeight);

	ok = check(GamepadEssentialUITestAccess::dispatchResourceListPointer(navigationScene)
		&& !GamepadEssentialUITestAccess::resourceListFocused(navigationScene)
		&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(
			navigationScene)
		&& GamepadEssentialUITestAccess::presentedSelectedResourceIndex(
			navigationScene) == 1,
		"resource list pointer takeover hides semantic focus but preserves the"
		" current selection frame") && ok;
	ok = check(navigationScene.handleUIAction(UIAction::NavigateDown)
		&& GamepadEssentialUITestAccess::resourceListFocused(navigationScene)
		&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(
			navigationScene)
		&& GamepadEssentialUITestAccess::selectedResourceIndex(navigationScene) == 2,
		"resource selection restores focus and performs the first semantic item step") && ok;

	ResourceSelectScene controlsScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(controlsScene, 800, 480);
	bool headerTraversalSucceeded =
		controlsScene.handleUIAction(UIAction::NavigateUp)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "check-updates"
		&& GamepadEssentialUITestAccess::presentedSelectedResourceIndex(
			controlsScene)
			== GamepadEssentialUITestAccess::selectedResourceIndex(
				controlsScene)
		&& controlsScene.handleUIAction(UIAction::NavigateLeft)
#if !defined(__MOBILE__)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "display-settings"
		&& controlsScene.handleUIAction(UIAction::NavigateLeft)
#endif
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "save-management"
		&& controlsScene.handleUIAction(UIAction::NavigateLeft)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "cheat-help";
	headerTraversalSucceeded = headerTraversalSucceeded
		&& controlsScene.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "save-management"
		&& controlsScene.handleUIAction(UIAction::NavigateRight)
#if !defined(__MOBILE__)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "display-settings"
		&& controlsScene.handleUIAction(UIAction::NavigateRight)
#endif
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "check-updates"
		&& controlsScene.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "exit"
		&& controlsScene.handleUIAction(UIAction::NavigateDown)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "resource-list";
	ok = check(headerTraversalSucceeded,
		"resource selection traverses its list and available header actions without"
		" hiding the selected card") && ok;
	ok = check(controlsScene.handleUIAction(UIAction::NavigateLeft)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "cheat-help"
		&& controlsScene.handleUIAction(UIAction::Confirm)
		&& GamepadEssentialUITestAccess::resourceCheatHelpIsModal(controlsScene)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "cheat-help-close"
		&& controlsScene.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "cheat-help-close"
		&& controlsScene.handleUIAction(UIAction::Cancel)
		&& !GamepadEssentialUITestAccess::resourceCheatHelpVisible(controlsScene)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "cheat-help"
		&& controlsScene.handleUIAction(UIAction::NavigateDown)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "resource-list",
		"resource selection cheat help blocks the underlying controls and"
		" restores header focus after dismissal") && ok;
	ResourceSelectScene cheatHelpPointerScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		cheatHelpPointerScene, 400, 480);
	ok = check(
		GamepadEssentialUITestAccess::openResourceCheatHelpWithPointer(
			cheatHelpPointerScene)
			&& GamepadEssentialUITestAccess::resourceCheatHelpIsModal(
				cheatHelpPointerScene)
			&& GamepadEssentialUITestAccess::closeResourceCheatHelpWithPointer(
				cheatHelpPointerScene)
			&& !GamepadEssentialUITestAccess::resourceCheatHelpVisible(
				cheatHelpPointerScene),
		"resource selection opens and closes cheat help through real pointer"
		" click frames") && ok;
	for (std::size_t index = 1; index < packs.size(); index++)
	{
		controlsScene.handleUIAction(UIAction::NavigateDown);
	}
#if defined(__ANDROID__) || \
	defined(JXQY_TEST_ANDROID_EXTERNAL_RESOURCE_UI)
	ok = check(GamepadEssentialUITestAccess::selectedResourceIndex(controlsScene)
			== static_cast<int>(packs.size()) - 1
		&& controlsScene.handleUIAction(UIAction::NavigateDown)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "enable-external"
		&& controlsScene.handleUIAction(UIAction::NavigateDown)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "external-link-0"
		&& controlsScene.handleUIAction(UIAction::NavigateUp)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "enable-external"
		&& controlsScene.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "check-updates"
		&& controlsScene.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "exit",
		"mobile resource selection reaches the external-resource toggle and"
		" external links before returning to exit") && ok;
#else
	ok = check(GamepadEssentialUITestAccess::selectedResourceIndex(controlsScene)
			== static_cast<int>(packs.size()) - 1
		&& controlsScene.handleUIAction(UIAction::NavigateDown)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "external-link-0"
		&& controlsScene.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "external-link-1"
		&& controlsScene.handleUIAction(UIAction::NavigateUp)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "resource-list"
		&& GamepadEssentialUITestAccess::navigateResourceSelectionToUpdate(
			controlsScene)
		&& controlsScene.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedResourceControl(controlsScene)
			== "exit",
		"resource selection navigates from the last card to the external"
		" links row and back through update to the exit action") && ok;
#endif
	controlsScene.setRunning(true);
	ok = check(controlsScene.handleUIAction(UIAction::Confirm)
		&& (controlsScene.result & erExit) != 0,
		"resource selection confirms its focused exit action") && ok;

	ResourceSelectScene pageScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(pageScene, 800, 480);
	const int visibleCount =
		GamepadEssentialUITestAccess::visibleResourceCount(pageScene);
	ok = check(visibleCount > 0
		&& pageScene.handleUIAction(UIAction::PanelNext)
		&& GamepadEssentialUITestAccess::selectedResourceIndex(pageScene)
			== visibleCount % static_cast<int>(packs.size()),
		"resource selection maps panel-next to a visible-page step") && ok;

	ResourceSelectScene cancelScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(cancelScene, 800, 480);
	cancelScene.setRunning(true);
	ok = check(cancelScene.handleUIAction(UIAction::Cancel)
		&& (cancelScene.result & erExit) != 0,
		"resource selection consumes cancel and returns exit") && ok;

	ResourceSelectScene onlineCatalogScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		onlineCatalogScene, 800, 480);
	ResourceSelectScene sameVersionScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		sameVersionScene, 800, 480);
	const int sameVersionLocalPackIndex =
		GamepadEssentialUITestAccess::resourceEntryLocalPackIndex(
			sameVersionScene, 0);
	OnlineUpdate::Catalog sameVersionCatalog;
	OnlineUpdate::ResourcePackage sameVersionPackage;
	sameVersionPackage.gameId =
		packs[sameVersionLocalPackIndex].manifest.id;
	sameVersionPackage.displayName = "Same Version";
	sameVersionPackage.versionText =
		packs[sameVersionLocalPackIndex].manifest.releaseMetadata.displayVersion;
	sameVersionPackage.artifactPath = "resources/same-version.zip";
	sameVersionPackage.artifactSize = 1024;
	sameVersionPackage.crc32Hex = "11111111";
	OnlineUpdate::IncrementalResourcePackage sameVersionIncremental;
	sameVersionIncremental.artifactPath =
		"resources/same-version-incremental.zip";
	sameVersionIncremental.artifactSize = 256;
	sameVersionIncremental.crc32Hex = "22222222";
	sameVersionPackage.incrementalPackage = sameVersionIncremental;
	OnlineUpdate::IncrementalResourcePackage sameVersionFirstIncremental;
	sameVersionFirstIncremental.artifactPath =
		"resources/same-version-incremental-001.zip";
	sameVersionFirstIncremental.artifactSize = 128;
	sameVersionFirstIncremental.crc32Hex = "12121212";
	sameVersionPackage.incrementalChain = {
		sameVersionFirstIncremental, sameVersionIncremental };
	sameVersionCatalog.resourcePackages.emplace(
		OnlineUpdate::foldGameId(sameVersionPackage.gameId),
		sameVersionPackage);
	auto& mutablePacks = const_cast<std::vector<ResourceManager::ResourcePack>&>(
		resourceManager.getDiscoveredPacks());
	const std::string previousFullReceipt =
		mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
			installedArtifactCrc32;
	const std::string previousIncrementalReceipt =
		mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
			installedIncrementalArtifactCrc32;
	const std::string previousIncrementalChainReceipt =
		mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
			installedIncrementalChainCrc32s;
	mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
		installedArtifactCrc32 = sameVersionPackage.crc32Hex;
	mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
		installedIncrementalArtifactCrc32 = sameVersionIncremental.crc32Hex;
	mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
		installedIncrementalChainCrc32s =
			sameVersionFirstIncremental.crc32Hex + "," +
			sameVersionIncremental.crc32Hex;
	ok = check(
		sameVersionLocalPackIndex >= 0 &&
		!sameVersionPackage.versionText.empty() &&
		GamepadEssentialUITestAccess::applyOnlineCatalog(
			sameVersionScene, sameVersionCatalog) &&
		GamepadEssentialUITestAccess::selectResourceEntry(
			sameVersionScene,
			GamepadEssentialUITestAccess::resourceEntryIndexByGameId(
				sameVersionScene, sameVersionPackage.gameId)) &&
		GamepadEssentialUITestAccess::selectedResourceMatchesOnlineVersion(
			sameVersionScene) &&
		GamepadEssentialUITestAccess::resourceOnlineActionIsAvailable(
			sameVersionScene),
		"matching local and online versions are labelled as current while"
		" keeping the explicit re-download action") && ok;
	mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
		installedIncrementalChainCrc32s =
			sameVersionFirstIncremental.crc32Hex;
	ok = check(
		GamepadEssentialUITestAccess::applyOnlineCatalog(
			sameVersionScene, sameVersionCatalog) &&
		GamepadEssentialUITestAccess::selectResourceEntry(
			sameVersionScene,
			GamepadEssentialUITestAccess::resourceEntryIndexByGameId(
				sameVersionScene, sameVersionPackage.gameId)) &&
		GamepadEssentialUITestAccess::selectedResourceNeedsContinueUpdate(
			sameVersionScene),
		"an incomplete chain receipt is labelled as resource content that still"
		" needs the continue-update action") && ok;
	sameVersionScene.setRunning(true);
	GamepadEssentialUITestAccess::confirmSelectedResource(sameVersionScene);
	ok = check(
		GamepadEssentialUITestAccess::localEntryUpdatePromptIsVisible(
			sameVersionScene) &&
		GamepadEssentialUITestAccess::
			resourceInstallConfirmationUsesIncrementalOnly(sameVersionScene) &&
		GamepadEssentialUITestAccess::
			cancelLocalEntryUpdatePromptWithKeyboard(sameVersionScene),
		"entering an installed resource detects an incomplete incremental chain"
		" even when its display version is unchanged, and Back dismisses the"
		" prompt without entering") && ok;
	GamepadEssentialUITestAccess::confirmSelectedResource(sameVersionScene);
	ok = check(
		GamepadEssentialUITestAccess::localEntryUpdatePromptIsVisible(
			sameVersionScene) &&
		GamepadEssentialUITestAccess::
			activateLocalEntryUpdateWithKeyboardAcrossFrames(sameVersionScene),
		"the entry update prompt activates its update action only after keyboard"
		" focus movement and confirmation occur in separate engine frames") && ok;
	sameVersionScene.handleUIAction(UIAction::Cancel);
	GamepadEssentialUITestAccess::confirmSelectedResource(sameVersionScene);
	ok = check(
		GamepadEssentialUITestAccess::localEntryUpdatePromptIsVisible(
			sameVersionScene) &&
		GamepadEssentialUITestAccess::
			enterWithoutLocalUpdateWithTouchAcrossFrames(sameVersionScene),
		"the entry update prompt allows explicitly entering the installed copy"
		" through touch down and up frames separated by a layout pass") && ok;
	mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
		installedIncrementalArtifactCrc32 =
			sameVersionIncremental.crc32Hex;
	mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
		installedIncrementalChainCrc32s =
			sameVersionFirstIncremental.crc32Hex + "," +
			sameVersionIncremental.crc32Hex;
	sameVersionScene.setRunning(true);
	GamepadEssentialUITestAccess::confirmSelectedResource(sameVersionScene);
	ok = check(
		!GamepadEssentialUITestAccess::resourceSceneRunning(sameVersionScene) &&
		!GamepadEssentialUITestAccess::localEntryUpdatePromptIsVisible(
			sameVersionScene),
		"entering a resource whose full and incremental receipts already match"
		" does not show a redundant update prompt") && ok;
	mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
		installedArtifactCrc32 = previousFullReceipt;
	mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
		installedIncrementalArtifactCrc32 = previousIncrementalReceipt;
	mutablePacks[sameVersionLocalPackIndex].manifest.releaseMetadata.
		installedIncrementalChainCrc32s = previousIncrementalChainReceipt;
	OnlineUpdate::Catalog testCatalog;
	OnlineUpdate::CommonPackage commonPackage;
	commonPackage.versionText = "1.0-test";
	commonPackage.artifactPath = "resources/common.zip";
	commonPackage.artifactSize = 4096;
	commonPackage.crc32Hex = "00000000";
	commonPackage.releaseNotes = "Common runtime fixture";
	testCatalog.commonPackage = commonPackage;
	OnlineUpdate::ResourcePackage localOnlinePackage;
	localOnlinePackage.gameId = packs.front().manifest.id;
	localOnlinePackage.displayName = "Online Local Match";
	localOnlinePackage.author = "Online Author";
	localOnlinePackage.versionText = "9.9-test";
	localOnlinePackage.artifactPath = "resources/local-match.zip";
	localOnlinePackage.artifactSize = 1024;
	localOnlinePackage.crc32Hex = "01010101";
	localOnlinePackage.releaseNotes = "Local resource update fixture";
	testCatalog.resourcePackages.emplace(
		OnlineUpdate::foldGameId(localOnlinePackage.gameId),
		localOnlinePackage);
	OnlineUpdate::ResourcePackage resourceOnlyPackage;
	resourceOnlyPackage.gameId = "ONLINE_SHARED_ONLY";
	resourceOnlyPackage.displayName = "Online Shared Only";
	resourceOnlyPackage.versionText = "1.0-test";
	resourceOnlyPackage.artifactPath = "resources/online-shared-only.zip";
	resourceOnlyPackage.artifactSize = 512;
	resourceOnlyPackage.crc32Hex = "02020202";
	resourceOnlyPackage.releaseNotes = "Dependency resource update fixture";
	resourceOnlyPackage.resourceOnly = true;
	resourceOnlyPackage.dependencyGameIds.push_back(
		localOnlinePackage.gameId);
	testCatalog.resourcePackages.emplace(
		OnlineUpdate::foldGameId(resourceOnlyPackage.gameId),
		resourceOnlyPackage);
	OnlineUpdate::ResourcePackage onlineOnlyPackage;
	onlineOnlyPackage.gameId = "ONLINE_TEST_ONLY";
	onlineOnlyPackage.displayName = "Online Only Test";
	onlineOnlyPackage.author = "Online Author";
	onlineOnlyPackage.versionText = "1.0-test";
	onlineOnlyPackage.releaseNotes = "Online-only resource fixture";
	onlineOnlyPackage.artifactPath = "resources/online-test-only.zip";
	onlineOnlyPackage.artifactSize = 2048;
	onlineOnlyPackage.crc32Hex = "03030303";
	onlineOnlyPackage.dependencyGameIds.push_back(
		resourceOnlyPackage.gameId);
	testCatalog.resourcePackages.emplace(
		OnlineUpdate::foldGameId(onlineOnlyPackage.gameId),
		onlineOnlyPackage);
	std::filesystem::path expectedLocalTarget =
		std::filesystem::u8path(packs.front().rootPath).lexically_normal();
	if (expectedLocalTarget.filename().empty())
	{
		expectedLocalTarget = expectedLocalTarget.parent_path();
	}
	const std::string expectedLocalTargetName =
		expectedLocalTarget.filename().generic_u8string();
	const bool onlineCatalogApplied =
		GamepadEssentialUITestAccess::applyOnlineCatalog(
			onlineCatalogScene, std::move(testCatalog));
	const int localOnlineEntryIndex =
		GamepadEssentialUITestAccess::resourceEntryIndexByGameId(
			onlineCatalogScene, localOnlinePackage.gameId);
	const int onlineOnlyEntryIndex =
		GamepadEssentialUITestAccess::resourceEntryIndexByGameId(
			onlineCatalogScene, onlineOnlyPackage.gameId);
	const int resourceOnlyEntryIndex =
		GamepadEssentialUITestAccess::resourceEntryIndexByGameId(
			onlineCatalogScene, resourceOnlyPackage.gameId);
	const bool onlineCatalogShapeReady =
		onlineCatalogApplied
		&& GamepadEssentialUITestAccess::resourceEntryCount(
				onlineCatalogScene) == static_cast<int>(packs.size()) + 1
		&& localOnlineEntryIndex >= 0
		&& onlineOnlyEntryIndex >= 0
		&& resourceOnlyEntryIndex < 0;
	ok = check(onlineCatalogShapeReady,
		"resource selection merges playable catalog entries and hides resource-only dependencies") && ok;
	const bool localOnlineEntryReady = onlineCatalogShapeReady
		&& GamepadEssentialUITestAccess::selectResourceEntry(
			onlineCatalogScene, localOnlineEntryIndex)
		&& GamepadEssentialUITestAccess::selectedResourceHasOnlineVersion(
			onlineCatalogScene, "9.9-test", false)
		&& GamepadEssentialUITestAccess::resourceOnlineActionIsAvailable(
			onlineCatalogScene);
	ok = check(localOnlineEntryReady,
		"an installed resource with an online package exposes its download action") && ok;
	const bool localDownloadConfirmationReady = localOnlineEntryReady
		&& GamepadEssentialUITestAccess::
			beginSelectedResourceDownloadConfirmation(onlineCatalogScene)
		&& GamepadEssentialUITestAccess::
			resourceInstallConfirmationItemCount(onlineCatalogScene) == 2
		&& GamepadEssentialUITestAccess::resourceInstallConfirmationKind(
			onlineCatalogScene, true, false, true)
		&& GamepadEssentialUITestAccess::resourceInstallConfirmationContains(
			onlineCatalogScene,
			localOnlinePackage.gameId,
			expectedLocalTargetName,
			true)
		&& GamepadEssentialUITestAccess::resourceInstallConfirmationContains(
			onlineCatalogScene, "common", "common", true)
		&& GamepadEssentialUITestAccess::
			resourceInstallConfirmationContainsReleaseNotes(
				onlineCatalogScene,
				localOnlinePackage.gameId,
				localOnlinePackage.releaseNotes)
		&& GamepadEssentialUITestAccess::
			resourceInstallConfirmationContainsReleaseNotes(
				onlineCatalogScene, "common", commonPackage.releaseNotes)
		&& GamepadEssentialUITestAccess::
			resourceInstallConfirmationUsesOnePagePerItem(onlineCatalogScene);
	ok = check(localDownloadConfirmationReady,
		"a local replacement records its exact target and required common package") && ok;
	const bool localDownloadCancelled = localDownloadConfirmationReady
		&& GamepadEssentialUITestAccess::
			cancelResourceInstallWithTouchAcrossLayout(onlineCatalogScene);
	ok = check(localDownloadCancelled,
		"the resource download confirmation closes through its touch action") && ok;
	const bool onlineOnlyEntrySelected = localDownloadCancelled
		&& GamepadEssentialUITestAccess::selectResourceEntry(
			onlineCatalogScene, onlineOnlyEntryIndex);
	ok = check(onlineOnlyEntrySelected,
		"the merged list selects its online-only entry") && ok;
	const bool onlineOnlyDetailsReady = onlineOnlyEntrySelected
		&& GamepadEssentialUITestAccess::selectedResourceHasOnlineVersion(
			onlineCatalogScene, "1.0-test", true);
	ok = check(onlineOnlyDetailsReady,
		"the online-only entry exposes its catalog version in details") && ok;
	const bool onlineOnlyPresentationReady = onlineOnlyDetailsReady
		&& GamepadEssentialUITestAccess::resourceCardShowsOnlineOnlyStatus(
			onlineCatalogScene, onlineOnlyEntryIndex);
	ok = check(onlineOnlyPresentationReady,
		"an online-only resource is visibly marked as not downloaded") && ok;
	onlineCatalogScene.setRunning(true);
	ok = check(
		onlineCatalogScene.handleUIAction(UIAction::Confirm)
			&& GamepadEssentialUITestAccess::resourceSceneRunning(
				onlineCatalogScene)
			&& GamepadEssentialUITestAccess::
				resourceInstallConfirmationItemCount(onlineCatalogScene) == 4
			&& GamepadEssentialUITestAccess::resourceInstallConfirmationKind(
				onlineCatalogScene, false, false, true)
			&& GamepadEssentialUITestAccess::resourceInstallConfirmationContains(
				onlineCatalogScene,
				localOnlinePackage.gameId,
				expectedLocalTargetName,
				true)
			&& GamepadEssentialUITestAccess::resourceInstallConfirmationContains(
				onlineCatalogScene,
				resourceOnlyPackage.gameId,
				"online-shared-only",
				false)
			&& GamepadEssentialUITestAccess::resourceInstallConfirmationContains(
				onlineCatalogScene,
				onlineOnlyPackage.gameId,
				"online-test-only",
				false)
			&& GamepadEssentialUITestAccess::resourceInstallConfirmationContains(
				onlineCatalogScene, "common", "common", true)
			&& GamepadEssentialUITestAccess::
				resourceInstallConfirmationContainsReleaseNotes(
					onlineCatalogScene,
					resourceOnlyPackage.gameId,
					resourceOnlyPackage.releaseNotes)
			&& GamepadEssentialUITestAccess::
				resourceInstallConfirmationContainsReleaseNotes(
					onlineCatalogScene,
					onlineOnlyPackage.gameId,
					onlineOnlyPackage.releaseNotes),
		"confirming an online-only resource presents its complete dependency"
		" closure and required runtime files without leaving the"
		" resource-selection scene") && ok;
	ok = check(
		GamepadEssentialUITestAccess::
			startInvalidResourceInstallWithMouseAcrossLayout(onlineCatalogScene)
			&& GamepadEssentialUITestAccess::
				cancelResourceInstallConfirmation(onlineCatalogScene)
			&& GamepadEssentialUITestAccess::
				beginSelectedResourceDownloadConfirmation(onlineCatalogScene),
		"resource download confirm and cancel buttons preserve real mouse and"
		" touch presses across the layout pass between down and up frames") && ok;
	ok = check(
		GamepadEssentialUITestAccess::rejectMissingStagedResourceActivation(
			onlineCatalogScene)
			&& GamepadEssentialUITestAccess::resourceSceneRunning(
				onlineCatalogScene),
		"resource-selection activation fails visibly when no staged transaction"
		" exists and never closes the current scene") && ok;
	ok = check(
		GamepadEssentialUITestAccess::cancelResourceInstallConfirmation(
			onlineCatalogScene),
		"closing a resource activation failure restores direct local entry") && ok;
	ok = check(
		GamepadEssentialUITestAccess::presentCancelledResourceDownload(
			onlineCatalogScene),
		"cancelling a resource download hides modal actions and restores the"
		" current resource list") && ok;
#if !defined(__ANDROID__) && !defined(__APPLE__)
	const std::filesystem::path temporaryRoot =
		makeUniqueTestDirectory("jxqy_gamepad_resource_selection_test");
	std::error_code errorCode;
	std::filesystem::remove_all(temporaryRoot, errorCode);
	std::filesystem::create_directories(temporaryRoot, errorCode);
	const std::filesystem::path temporaryAssetsRoot =
		temporaryRoot / "assets";
	std::filesystem::create_directories(
		temporaryAssetsRoot, errorCode);
	File::setAssetsCollectionRoot(
		temporaryAssetsRoot.generic_string());

	ResourceSelectScene directEntryScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		directEntryScene, 800, 480);
	const int targetEntryIndex =
		GamepadEssentialUITestAccess::resourceEntryCount(directEntryScene) > 1
			? 1 : 0;
	const int targetPackIndex =
		GamepadEssentialUITestAccess::resourceEntryLocalPackIndex(
			directEntryScene, targetEntryIndex);
	const std::string expectedId = packs[targetPackIndex].manifest.id;
	const std::string expectedRootPath = packs[targetPackIndex].rootPath;
	directEntryScene.setRunning(true);
	const bool directEntryDispatched =
		GamepadEssentialUITestAccess::dispatchRealResourceCardClick(
			directEntryScene, targetEntryIndex);
	ok = check(directEntryDispatched,
		"resource card click selects the requested production pack") && ok;
	ok = check(
		!GamepadEssentialUITestAccess::resourceSceneRunning(directEntryScene),
		"resource card click directly enters an installed production pack") && ok;
	ok = check(resourceManager.getActiveManifest().id == expectedId,
		"resource card click activates the selected production pack") && ok;
	std::unique_ptr<char[]> selectionData;
	int selectionLength = 0;
	const bool selectionRead = File::readSharedApplicationFile(
		"save/system/resource_selection.ini",
		selectionData,
		selectionLength,
		16 * 1024);
	if (selectionRead && selectionData != nullptr)
	{
		INIReader selection(selectionData);
		ok = check(selection.Get("ResourceSelection", "Id", "") == expectedId
			&& selection.Get("ResourceSelection", "RootPath", "") == expectedRootPath,
			"resource card direct entry persists the selected pack only in the"
			" temporary collection")
			&& ok;
	}
	else
	{
		ok = check(false,
			"resource selection writes its temporary recent-selection record") && ok;
	}
	ResourceSelectScene semanticConfirmScene;
	GamepadEssentialUITestAccess::prepareResourceSelection(
		semanticConfirmScene, 800, 480);
	const int semanticTargetEntryIndex =
		GamepadEssentialUITestAccess::resourceEntryIndexByGameId(
			semanticConfirmScene, expectedId);
	for (int index = 0; index < semanticTargetEntryIndex; index++)
	{
		semanticConfirmScene.handleUIAction(UIAction::NavigateDown);
	}
	semanticConfirmScene.setRunning(true);
	ok = check(
		semanticConfirmScene.handleUIAction(UIAction::Confirm)
			&& !GamepadEssentialUITestAccess::resourceSceneRunning(
				semanticConfirmScene)
			&& resourceManager.getActiveManifest().id == expectedId,
		"keyboard and gamepad confirm activate the highlighted resource from"
		" the list") && ok;
	File::setAssetsCollectionRoot(previousAssetsCollectionRoot);
	ok = check(resourceManager.setActiveResourcePackById("JXQY2"),
		"resource selection test restores the default production pack") && ok;
	std::filesystem::remove_all(temporaryRoot, errorCode);
#else
	// Mobile and Apple shared application files live under the platform
	// writable base rather than the injected collection root. Do not confirm in
	// this host-side contract because it must never touch a real user profile.
	ok = check(resourceManager.setActiveResourcePackById("JXQY2"),
		"resource selection test keeps the default production pack") && ok;
#endif
#if defined(__ANDROID__) || \
	defined(JXQY_TEST_ANDROID_EXTERNAL_RESOURCE_UI)
	Config::externalResourcesEnabled = originalExternalResourcesEnabled;
#endif
	return ok;
}

bool testResourceSelectionPhysicalFirstAction()
{
	bool ok = check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"resource selection physical-link test started without SDL video");
	VirtualGamepadTest::SDLSession sdlSession;
	VirtualGamepadTest::VirtualGamepad gamepad(
		"JXQY Resource Selection Focus Pad");
	auto& inputManager = const_cast<GameInput::PhysicalInputManager&>(
		Engine::getInstance()->inputActions());
	HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(inputManager);
	if (!check(inputScope.isInitialized(),
		"resource selection physical input manager initialized"))
	{
		return false;
	}

	auto scene = std::make_shared<ResourceSelectScene>();
	GamepadEssentialUITestAccess::prepareResourceSelection(*scene, 800, 480);
	HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(scene);
	const std::string activeRootBeforeNavigation =
		ResourceManager::instance().getActiveResourceRoot();
	ok = check(!inputManager.hasActiveGamepad()
		&& !GamepadEssentialUITestAccess::resourceListFocused(*scene)
		&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(*scene)
		&& GamepadEssentialUITestAccess::presentedSelectedResourceIndex(*scene) == 0,
		"resource selection keeps the default candidate visible while the"
		" connected pad is still inactive") && ok;

	std::uint64_t nowMilliseconds = SDL_GetTicks();
	HeadlessPhysicalInputTest::FrameDriver frameDriver(
		inputManager,
		nowMilliseconds,
		[]()
		{
			return dispatchPhysicalUIActions(Engine::getInstance());
		},
		{});
	frameDriver.runFrame();
	bool navigatePressed = false;
	bool navigateConsumed = false;
	HeadlessPhysicalInputTest::FrameCallbacks callbacks;
	callbacks.afterInputUpdate =
		[&navigatePressed](const GameInput::PhysicalInputManager& frameInputManager)
	{
		navigatePressed = frameInputManager.wasActionPressed(
			GameInput::InputAction::NavigateDown);
	};
	callbacks.afterDispatch =
		[&navigateConsumed, &inputManager](bool)
	{
		navigateConsumed = !inputManager.wasActionPressed(
			GameInput::InputAction::NavigateDown);
	};
	const bool dispatched = frameDriver.tapButton(
		gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN, callbacks);
	ok = check(navigatePressed
		&& navigateConsumed
		&& dispatched
		&& inputManager.hasActiveGamepad()
		&& GamepadEssentialUITestAccess::resourceListFocused(*scene)
		&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(*scene)
		&& GamepadEssentialUITestAccess::selectedResourceIndex(*scene) == 1
		&& GamepadEssentialUITestAccess::resourceDetailMatchesSelection(
			*scene, 1)
		&& ResourceManager::instance().getActiveResourceRoot()
			== activeRootBeforeNavigation,
		"resource selection restores focus, previews details, and performs the"
		" first physical item step without activating the MOD") && ok;

	ok = check(GamepadEssentialUITestAccess::dispatchResourceListPointer(*scene)
		&& !GamepadEssentialUITestAccess::resourceListFocused(*scene)
		&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(*scene)
		&& GamepadEssentialUITestAccess::presentedSelectedResourceIndex(*scene) == 1,
		"resource selection pointer takeover hides physical focus but preserves"
		" the current selection frame") && ok;
	ok = check(frameDriver.tapButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)
		&& GamepadEssentialUITestAccess::resourceListFocused(*scene)
		&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(*scene)
		&& GamepadEssentialUITestAccess::selectedResourceIndex(*scene) == 2
		&& GamepadEssentialUITestAccess::resourceDetailMatchesSelection(
			*scene, 2)
		&& ResourceManager::instance().getActiveResourceRoot()
			== activeRootBeforeNavigation,
		"resource selection replays the first physical action after pointer"
		" takeover and refreshes details without activating the MOD") && ok;

	inputManager.shutdown();
	GamepadEssentialUITestAccess::synchronizeResourceFocusWithInput(*scene);
	ok = check(!GamepadEssentialUITestAccess::resourceListFocused(*scene)
		&& GamepadEssentialUITestAccess::resourceSelectionIndicatorVisible(*scene)
		&& GamepadEssentialUITestAccess::presentedSelectedResourceIndex(*scene) == 2,
		"resource selection hides physical focus after active-pad removal while"
		" preserving the current selection frame") && ok;
	ok = check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"resource selection physical-link test initialized SDL video") && ok;
	return ok;
}

bool testTitleController(const ResourcePackExpectation& resourcePack)
{
	bool ok = true;
	Engine* engine = Engine::getInstance();
	engine->setWindowSize(1100, 500);
	Title title(true);
	title.init();
	const auto newGame = title.getComponentByName<Button>("initBtn");
	const auto loadGame = title.getComponentByName<Button>("loadBtn");
	const auto team = title.getComponentByName<Button>("teamBtn");
	const auto exit = title.getComponentByName<Button>("exitBtn");
	const auto systemNotice =
		GamepadEssentialUITestAccess::titleSystemNotice(title);
	const bool complete = newGame != nullptr && loadGame != nullptr
		&& team != nullptr && exit != nullptr && systemNotice != nullptr;
	if (!checkPack(complete, resourcePack,
		"title loads all four production actions"))
	{
		engine->setWindowSize(800, 600);
		return false;
	}

	constexpr float MobileTitleScale = 500.0f / 480.0f;
	const int fittedTitleWidth = static_cast<int>(std::round(640 * MobileTitleScale));
	const int fittedTitleOffsetX = (1100 - fittedTitleWidth) / 2;
	const Rect& sourceRect = resourcePack.titleNewGameRect;
	ok = checkPack(title.keepAspect && title.fadeMirroredBars &&
		title.baseWidth == 640 &&
		title.baseHeight == 480 &&
		newGame->rect.x == static_cast<int>(sourceRect.x * MobileTitleScale)
			+ fittedTitleOffsetX &&
		newGame->rect.y == static_cast<int>(sourceRect.y * MobileTitleScale) &&
		newGame->rect.w == static_cast<int>(sourceRect.w * MobileTitleScale) &&
		newGame->rect.h == static_cast<int>(sourceRect.h * MobileTitleScale),
		resourcePack,
		"title background and buttons share the 640x480 aspect-fit transform"
		" on a wide mobile viewport") && ok;

	title.setTime(1000);
	GamepadEssentialUITestAccess::previewTitlePointerEvent(
		title,
		AEvent(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 275, 125, false));
	GamepadEssentialUITestAccess::previewTitlePointerEvent(
		title,
		AEvent(ET_MOUSEDOWN, MBC_MOUSE_RIGHT, 550, 250, false));
	GamepadEssentialUITestAccess::previewTitlePointerEvent(
		title,
		AEvent(ET_FINGERDOWN, 37, 825, 375, false));
	GamepadEssentialUITestAccess::previewTitlePointerEvent(
		title,
		AEvent(ET_FINGERDOWN, 38, 1100, 500, false));
	const auto& pointerRipples =
		GamepadEssentialUITestAccess::titlePointerRipples(title);
	ok = checkPack(
		pointerRipples.size() == 2 &&
		std::abs(pointerRipples[0].normalizedX - 0.25f) < 0.001f &&
		std::abs(pointerRipples[0].normalizedY - 0.25f) < 0.001f &&
		std::abs(pointerRipples[1].normalizedX - 0.75f) < 0.001f &&
		std::abs(pointerRipples[1].normalizedY - 0.75f) < 0.001f,
		resourcePack,
		"title records left-mouse and finger ripple origins without accepting"
		" right-button or out-of-bounds presses") && ok;
	GamepadEssentialUITestAccess::previewTitlePointerEvent(
		title,
		AEvent(ET_FINGERDOWN, 39, 550, 125, false));
	GamepadEssentialUITestAccess::previewTitlePointerEvent(
		title,
		AEvent(ET_FINGERDOWN, 40, 550, 250, false));
	GamepadEssentialUITestAccess::previewTitlePointerEvent(
		title,
		AEvent(ET_FINGERDOWN, 41, 550, 375, false));
	const auto& boundedPointerRipples =
		GamepadEssentialUITestAccess::titlePointerRipples(title);
	ok = checkPack(
		boundedPointerRipples.size() == 4 &&
		std::abs(boundedPointerRipples.front().normalizedX - 0.25f) < 0.001f,
		resourcePack,
		"title bounds simultaneous ripples without abruptly replacing an"
		" active wave") && ok;
	title.setTime(
		1000 + AspectFitLayout::PointerRippleDurationMilliseconds);
	GamepadEssentialUITestAccess::removeExpiredTitlePointerRipples(title);
	ok = checkPack(
		GamepadEssentialUITestAccess::titlePointerRipples(title).empty(),
		resourcePack,
		"title removes pointer ripples after their bounded lifetime") && ok;

	const std::vector<std::shared_ptr<Button>> order =
		{ newGame, loadGame, team, exit };
	ok = checkPack(
		GamepadEssentialUITestAccess::focusedTitleControl(title) == newGame,
		resourcePack,
		"title defaults to new game") && ok;
	for (std::size_t index = 1; index <= order.size(); index++)
	{
		ok = checkPack(title.handleUIAction(UIAction::NavigateDown)
			&& GamepadEssentialUITestAccess::focusedTitleControl(title)
				== order[index % order.size()],
			resourcePack,
			"title follows and wraps its visible vertical action order") && ok;
	}
	ok = checkPack(title.handleUIAction(UIAction::NavigateUp)
		&& GamepadEssentialUITestAccess::focusedTitleControl(title) == exit,
		resourcePack,
		"title wraps upward from new game to exit") && ok;
	ok = checkPack(!title.handleUIAction(UIAction::Cancel)
		&& GamepadEssentialUITestAccess::focusedTitleControl(title) == exit,
		resourcePack,
		"title leaves cancel unhandled") && ok;

	GamepadEssentialUITestAccess::showTitleSceneFailureNotice(
		title,
		{},
		3,
		false,
		true);
	ok = checkPack(
		!systemNotice->visible && systemNotice->currentMessage.empty(),
		resourcePack,
		"title does not report an ordinary return without a failure reason") && ok;

	GamepadEssentialUITestAccess::showTitleSceneFailureNotice(
		title,
		u8"玩家存档 player3.ini 缺少 Init 段",
		3,
		false,
		true);
	ok = checkPack(
		systemNotice->visible &&
			systemNotice->currentMessage ==
				u8"系统：读档失败（存档槽 3）：玩家存档 player3.ini 缺少 Init 段。请保留日志以便进一步排查。",
		resourcePack,
		"title reports the selected save slot and concrete load failure") && ok;
	systemNotice->dismiss();
	GamepadEssentialUITestAccess::showTitleSceneFailureNotice(
		title,
		u8"地图切换提交失败",
		3,
		false,
		false);
	ok = checkPack(
		systemNotice->visible &&
			systemNotice->currentMessage ==
				u8"系统：游戏运行失败：地图切换提交失败。请保留日志以便进一步排查。",
		resourcePack,
		"title does not mislabel a later runtime failure as a save-slot load failure") && ok;
	systemNotice->dismiss();
	ok = checkPack(
		!systemNotice->visible && systemNotice->currentMessage.empty(),
		resourcePack,
		"title failure notices can be dismissed before the next loading transition") && ok;

	title.setRunning(true);
	ok = checkPack(title.handleUIAction(UIAction::Confirm)
		&& (title.result & erExit) != 0,
		resourcePack,
		"title confirms exit without entering a scene run loop") && ok;
	engine->setWindowSize(800, 600);
	return ok;
}

bool testPublishedModTitleResources(ResourceManager& resourceManager)
{
	const char* packIds[] = {
		"JXQY2",
		"YYCS",
		"JIAN_ER_GAI_CHENGHE_1_041",
		"JIANGHU_YUCHEN_1_03",
		"JIANGHU_YUCHEN_2",
		"XIAOXIANGXING_1_022",
		"XINYUE_WUHEN_3_0",
		"YUEMEIER_WAIZHUAN_1_053",
	};
	std::map<std::string, _shared_imp> titleImages;
	bool ok = true;
	for (const char* packId : packIds)
	{
		if (!resourceManager.setActiveResourcePackById(packId))
		{
			ok = check(false,
				std::string(packId) + " published title pack is available") && ok;
			continue;
		}
		Title title(true);
		title.init();
		titleImages[packId] = title.impImage;
		ok = check(title.impImage != nullptr && !title.impImage->frame.empty(),
			std::string(packId) + " loads a non-empty title background package") && ok;
	}

	ok = check(
		titleImages["JIANGHU_YUCHEN_1_03"] != nullptr &&
			titleImages["JIANGHU_YUCHEN_1_03"] != titleImages["YYCS"] &&
			titleImages["JIANGHU_YUCHEN_2"] != titleImages["JIANGHU_YUCHEN_1_03"] &&
			titleImages["XIAOXIANGXING_1_022"] != titleImages["YYCS"],
		"local MOD title backgrounds do not reuse another resource pack's cache entry") && ok;
	return check(resourceManager.setActiveResourcePackById("JXQY2"),
		"published title resource test restores JXQY2") && ok;
}

bool testRunningModalSubtreeSurvivesConfigDrivenResize(
	const ResourcePackExpectation& resourcePack)
{
	bool ok = true;
	Engine* engine = Engine::getInstance();
	engine->setWindowSize(1024, 720);

	auto verifyResize =
		[&ok, &resourcePack](
			Element& parent,
			const PElement& runningChild,
			const std::string& relationship)
		{
			parent.addChild(runningChild);
			HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(
				runningChild);
			GamepadEssentialUITestAccess::resizeElementTree(
				parent, 1024, 720);

			const std::size_t directChildCount = static_cast<std::size_t>(
				std::count_if(
					parent.children.begin(),
					parent.children.end(),
					[&runningChild](const PElement& child)
					{
						return child.get() == runningChild.get();
					}));
			ok = checkPack(
				runningChild->parent == &parent
					&& directChildCount == 1
					&& Element::isCurrentRunOwner(runningChild.get())
					&& Element::currentRunOwnerBlocksParentInput(),
				resourcePack,
				relationship
					+ " keeps the active dynamic child attached exactly once"
					" and preserves the nested input barrier across resize")
				&& ok;
		};

	{
		auto system = std::make_shared<System>();
		auto saveLoad = std::make_shared<SaveLoad>(true, true);
		saveLoad->setPriority(0);
		verifyResize(*system, saveLoad, "system to save-load");
	}
	{
		auto system = std::make_shared<System>();
		auto option = std::make_shared<Option>();
		option->setPriority(epMax);
		verifyResize(*system, option, "system to option");
	}
	{
		auto title = std::make_shared<Title>(true);
		title->init();
		auto saveLoad = std::make_shared<SaveLoad>(false, true);
		saveLoad->setPriority(epMax + 2);
		verifyResize(*title, saveLoad, "title to save-load");
		const auto newGame = title->getComponentByName<Button>("initBtn");
		const auto loadingFadeMask =
			GamepadEssentialUITestAccess::titleLoadingFadeMask(*title);
		const auto systemNotice =
			GamepadEssentialUITestAccess::titleSystemNotice(*title);
		const auto weather = GamepadEssentialUITestAccess::titleWeather(*title);
		ok = checkPack(
			loadingFadeMask != nullptr && systemNotice != nullptr &&
			weather != nullptr &&
			GamepadEssentialUITestAccess::titleDrawsChildAfterComposition(
				*title, saveLoad) &&
			GamepadEssentialUITestAccess::titleDrawsChildAfterComposition(
				*title, loadingFadeMask) &&
			GamepadEssentialUITestAccess::titleDrawsChildAfterComposition(
				*title, systemNotice) &&
			!GamepadEssentialUITestAccess::titleDrawsChildAfterComposition(
				*title, weather) &&
			!GamepadEssentialUITestAccess::titleDrawsChildAfterComposition(
				*title, newGame) &&
			systemNotice->getPriority() <
				loadingFadeMask->getPriority() &&
			loadingFadeMask->getPriority() <
				saveLoad->getPriority() &&
			saveLoad->getPriority() < weather->getPriority() &&
			!loadingFadeMask->visible && !systemNotice->visible,
			resourcePack,
			"title defers save-load, loading fade, and the hidden system notice"
			" in ascending overlay order without lifting weather or ordinary"
			" controls out of the mirrored composition") && ok;
	}

	engine->setWindowSize(800, 600);
	return ok;
}

bool testTitlePhysicalExitLink()
{
	bool ok = check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"title physical-link test started without SDL video");
	VirtualGamepadTest::SDLSession sdlSession;
	VirtualGamepadTest::VirtualGamepad gamepad(
		"JXQY Essential UI Title Pad");
	auto title = std::make_shared<Title>(true);
	title->init();
	const auto newGame = title->getComponentByName<Button>("initBtn");
	const auto loadGame = title->getComponentByName<Button>("loadBtn");
	const auto team = title->getComponentByName<Button>("teamBtn");
	const auto exit = title->getComponentByName<Button>("exitBtn");
	if (!check(newGame != nullptr && loadGame != nullptr
		&& team != nullptr && exit != nullptr,
		"title physical-link fixture loaded all four production actions"))
	{
		return false;
	}

	auto& inputManager = const_cast<GameInput::PhysicalInputManager&>(
		Engine::getInstance()->inputActions());
	HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
		inputManager);
	HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(title);
	ok = check(inputScope.isInitialized(),
		"title physical-link input manager initialized") && ok;
	if (!inputScope.isInitialized())
	{
		return false;
	}

	std::uint64_t nowMilliseconds = SDL_GetTicks();
	HeadlessPhysicalInputTest::FrameDriver frameDriver(
		inputManager,
		nowMilliseconds,
		[]()
		{
			return dispatchPhysicalUIActions(Engine::getInstance());
		},
		{});
	frameDriver.runFrame();
	const std::vector<std::shared_ptr<Button>> expectedFocusOrder =
		{ loadGame, team, exit };
	for (std::size_t index = 0; index < expectedFocusOrder.size(); index++)
	{
		bool navigatePressed = false;
		HeadlessPhysicalInputTest::FrameCallbacks callbacks;
		callbacks.afterInputUpdate =
			[&navigatePressed](
				const GameInput::PhysicalInputManager& frameInputManager)
		{
			navigatePressed = frameInputManager.wasActionPressed(
				GameInput::InputAction::NavigateDown);
		};
		const bool dispatched = frameDriver.tapButton(
			gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN, callbacks);
		ok = check(navigatePressed
			&& dispatched
			&& expectedFocusOrder[index]->isFocused()
			&& title->getResult() == erNone,
			"physical D-pad down did not follow the safe title focus path at step "
				+ std::to_string(index + 1)) && ok;
	}

	title->setRunning(true);
	bool confirmPressed = false;
	bool confirmConsumed = false;
	HeadlessPhysicalInputTest::FrameCallbacks confirmCallbacks;
	confirmCallbacks.afterInputUpdate =
		[&confirmPressed](
			const GameInput::PhysicalInputManager& frameInputManager)
	{
		confirmPressed = frameInputManager.wasActionPressed(
			GameInput::InputAction::Confirm);
	};
	confirmCallbacks.afterDispatch =
		[&confirmConsumed, &inputManager](bool)
	{
		confirmConsumed = !inputManager.wasActionPressed(
			GameInput::InputAction::Confirm);
	};
	const bool confirmDispatched = frameDriver.tapButton(
		gamepad, SDL_GAMEPAD_BUTTON_SOUTH, confirmCallbacks);
	ok = check(confirmPressed
		&& confirmDispatched
		&& (title->getResult() & erExit) != 0
		&& confirmConsumed,
		"physical A did not consume Confirm on the focused safe exit action") && ok;
	ok = check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"title physical-link test initialized SDL video") && ok;
	return ok;
}

bool testActionOnlyModalSurfaces()
{
	bool ok = true;
	TitleTeam videoTeamPage(
		"team.avi");
	ok = check(
		GamepadEssentialUITestAccess::prepareVideoTitleTeam(videoTeamPage)
			&& GamepadEssentialUITestAccess::isVideoOnlyTitleTeam(
				videoTeamPage),
		"title team video owns the full screen without an engine-credit"
		" or text overlay") && ok;
	videoTeamPage.setRunning(true);
	ok = check(
		videoTeamPage.handleUIAction(UIAction::Confirm)
			&& !GamepadEssentialUITestAccess::isTitleTeamRunning(
				videoTeamPage),
		"title team full-screen video handles controller confirmation as"
		" a skip action") && ok;
	videoTeamPage.setRunning(true);
	ok = check(
		GamepadEssentialUITestAccess::dispatchTitleTeamKey(
			videoTeamPage, KEY_RETURN)
			&& !GamepadEssentialUITestAccess::isTitleTeamRunning(
				videoTeamPage),
		"title team full-screen video handles Enter as a skip action") && ok;
	videoTeamPage.setRunning(true);
	ok = check(
		GamepadEssentialUITestAccess::dispatchTitleTeamKey(
			videoTeamPage, KEY_SPACE)
			&& !GamepadEssentialUITestAccess::isTitleTeamRunning(
				videoTeamPage),
		"title team full-screen video handles Space as a skip action") && ok;
	videoTeamPage.setRunning(true);
	videoTeamPage.vp->result = erVideoStopped;
	videoTeamPage.onChildCallBack(videoTeamPage.vp);
	ok = check(
		!GamepadEssentialUITestAccess::isTitleTeamRunning(videoTeamPage),
		"title team full-screen video closes when its player reports"
		" completion") && ok;
	videoTeamPage.setRunning(true);
	GamepadEssentialUITestAccess::updateTitleTeam(videoTeamPage);
	ok = check(
		!GamepadEssentialUITestAccess::isTitleTeamRunning(videoTeamPage),
		"title team full-screen video fails closed when video loading"
		" produces no player") && ok;
	videoTeamPage.freeResource();

	TitleTeam combinedTeamPage("team.avi", "Resource-pack Team information");
	ok = check(
		GamepadEssentialUITestAccess::prepareVideoTitleTeam(combinedTeamPage)
			&& GamepadEssentialUITestAccess::isFullScreenVideoAndTextTitleTeam(
				combinedTeamPage),
		"title team keeps an explicitly configured video full-screen when"
		" Team-info is also present") && ok;
	combinedTeamPage.freeResource();

	TitleTeam teamPage("", "Controller modal surface text");
	teamPage.setRunning(true);
	ok = check(
		teamPage.handleUIAction(UIAction::NavigateDown),
		"title team surface handles controller scrolling without"
		" creating a fake focus target") && ok;
	ok = check(
		teamPage.handleUIAction(UIAction::Confirm),
		"title team surface handles controller confirmation as close") && ok;

	VideoPage videoPage;
	videoPage.setRunning(true);
	ok = check(
		videoPage.handleUIAction(UIAction::Cancel)
			&& (videoPage.getResult() & erVideoStopped) != 0,
		"video playback handles controller cancel as a skip action") && ok;
	return ok;
}

bool testDisplayOnlyAndPassiveSurfaceContracts(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	gameManager.menu->init();
	MenuController& menu = *gameManager.menu;
	bool ok = checkPack(
		menu.stateMenu != nullptr
			&& menu.messageBox != nullptr
			&& menu.systemNotice != nullptr
			&& menu.timerMenu != nullptr
			&& menu.toolTip != nullptr
			&& menu.npcInfoPanel != nullptr
			&& menu.bottomMenu != nullptr,
		resourcePack,
		"production menu creation path instantiates every display-only overlay");
	if (!ok)
	{
		return false;
	}
	if (resourcePack.gameType == GAME_JXQY2)
	{
		auto lifeLabel = menu.stateMenu->getComponentByName<Label>("labLife");
		auto thewLabel = menu.stateMenu->getComponentByName<Label>("labThew");
		auto manaLabel = menu.stateMenu->getComponentByName<Label>("labMana");
		const int stateContentRight =
			menu.stateMenu->rect.x + menu.stateMenu->rect.w - 4;
		ok = checkPack(
			lifeLabel != nullptr && thewLabel != nullptr && manaLabel != nullptr &&
				lifeLabel->fontSize == 16 && lifeLabel->autoShrink &&
				thewLabel->fontSize == 16 && thewLabel->autoShrink &&
				manaLabel->fontSize == 16 && manaLabel->autoShrink &&
				lifeLabel->rect.x + lifeLabel->rect.w <= stateContentRight &&
				thewLabel->rect.x + thewLabel->rect.w <= stateContentRight &&
				manaLabel->rect.x + manaLabel->rect.w <= stateContentRight,
			resourcePack,
			"status values use compact adaptive text inside the panel boundary") && ok;
		ok = checkPack(
			menu.toolTip->name != nullptr && menu.toolTip->cost != nullptr &&
				menu.toolTip->intro1 != nullptr && menu.toolTip->intro2 != nullptr &&
				menu.toolTip->name->fontSize == 18 &&
				menu.toolTip->cost->fontSize == 18 &&
				menu.toolTip->intro1->fontSize == 16 &&
				menu.toolTip->intro2->fontSize == 16 &&
				menu.toolTip->intro2->fontSize * 4 <=
					menu.toolTip->intro2->rect.h,
			resourcePack,
			"item and skill tooltip text keeps four compact lines above the lower border") && ok;
	}
	ok = checkPack(
		menu.messageBox->parent == menu.upMenu.get()
			&& menu.dialog != nullptr
			&& menu.dialog->parent == &menu
			&& menu.systemNotice != nullptr
			&& menu.systemNotice->parent == &menu
			&& menu.systemNotice->hasFont()
			&& menu.systemNotice->children.empty()
			&& menu.upMenu != nullptr
			&& menu.systemNotice->getPriority()
				< menu.dialog->getPriority()
			&& menu.dialog->getPriority()
				< menu.upMenu->getPriority()
			&& menu.systemNotice->rect.y
				< menu.messageBox->rect.y,
		resourcePack,
		"system notices stay above story dialogs while gameplay messages keep"
		" their ordinary lower-screen overlay layer")
		&& ok;

	menu.clearMenu();
	menu.cancelControllerInteraction();
	menu.messageBox->currentMessage.clear();
	menu.messageBox->showed = false;
	menu.messageBox->visible = false;
	const int moneyBeforePickup = gameManager.player->money;
	gameManager.scriptAPI.addMoney(37);
	ok = checkPack(
		gameManager.player->money == moneyBeforePickup + 37
			&& menu.messageBox->currentMessage == u8"获得37两银子！"
			&& menu.messageBox->visible
			&& menu.messageBox->showed
			&& menu.messageBox->label != nullptr
			&& menu.messageBox->label->visible
			&& menu.messageBox->label->activated
			&& menu.messageBox->label->getStr()
				== menu.messageBox->currentMessage,
		resourcePack,
		"script money pickup populates one visible message-box notification")
		&& ok;
	gameManager.player->money = 0;
	gameManager.scriptAPI.addMoney(std::numeric_limits<int>::min());
	ok = checkPack(
		gameManager.player->money == std::numeric_limits<int>::min()
			&& menu.messageBox->currentMessage ==
				u8"失去2147483648两银子！",
		resourcePack,
		"script money loss formats the full minimum integer without signed overflow")
		&& ok;
	if (resourcePack.gameType == GAME_YYCS)
	{
		const Rect& panelRect = menu.messageBox->rect;
		const std::shared_ptr<Label>& messageLabel = menu.messageBox->label;
		ok = checkPack(
			panelRect.x == 272 && panelRect.y == 435
				&& panelRect.w == 236 && panelRect.h == 94
				&& messageLabel != nullptr
				&& messageLabel->rect.x == 318 && messageLabel->rect.y == 467
				&& messageLabel->rect.w == 148 && messageLabel->rect.h == 50
				&& messageLabel->fontSize == 20
				&& messageLabel->color == 0xCC9B2216,
			resourcePack,
			"money notification stays above the bottom menu and preserves its text style")
			&& ok;

		gameManager.player->money = moneyBeforePickup;
		menu.messageBox->currentMessage.clear();
		menu.messageBox->showed = false;
		menu.messageBox->visible = false;
		gameManager.scriptAPI.runScript(u8"1级钱.txt");
		const int moneyGained =
			gameManager.player->money - moneyBeforePickup;
		ok = checkPack(
			moneyGained >= 10 && moneyGained <= 40
				&& menu.messageBox->currentMessage ==
					u8"获得" + std::to_string(moneyGained) +
					u8"两银子！"
				&& menu.messageBox->visible
				&& menu.messageBox->showed,
			resourcePack,
			"level-one defeated-NPC money script increases money and shows the matching pickup message")
			&& ok;
	}
	gameManager.player->money = moneyBeforePickup;
	menu.messageBox->currentMessage.clear();
	menu.messageBox->showed = false;
	menu.messageBox->visible = false;
	menu.stateMenu->visible = true;
	ok = checkPack(!menu.stateMenu->needEvents
			&& menu.blocksWorldInput()
			&& !menu.blocksWorldPointerInput()
			&& !isUIFocusElementAvailable(menu.stateMenu),
		resourcePack,
		"display-only state surface blocks world semantics without pointer"
		" capture or an empty focus target") && ok;
	menu.stateMenu->visible = false;

	menu.messageBox->showMessage("controller passive overlay", 1000);
	menu.showSystemNotice("controller system overlay", 1000);
	menu.timerMenu->startTimer(61);
	menu.toolTip->visible = true;
	menu.npcInfoPanel->visible = true;
	const std::array<PElement, 5> passiveOverlays =
	{
		menu.messageBox,
		menu.systemNotice,
		menu.timerMenu,
		menu.toolTip,
		menu.npcInfoPanel
	};
	ok = checkPack(std::all_of(
			passiveOverlays.begin(),
			passiveOverlays.end(),
			[](const PElement& surface)
			{
				return surface != nullptr && surface->visible
					&& !surface->needEvents
					&& !surface->hasPointerDownInTree(TOUCH_MOUSEID)
					&& !isUIFocusElementAvailable(surface);
			}),
		resourcePack,
		"message, timer, tooltip, and NPC info stay visible while remaining"
		" outside pointer and focus dispatch") && ok;

	if (gameManager.global.feature.topButtonsLayout)
	{
		ok = checkPack(menu.topMenu != nullptr && menu.columnMenu != nullptr
				&& menu.columnMenu->visible
				&& !menu.columnMenu->needEvents
				&& !isUIFocusElementAvailable(menu.columnMenu)
				&& !menu.topMenu->controllerFocusCandidates().empty()
				&& menu.topMenu->activateControllerFocus(
					ControllerFocusTarget::Default),
			resourcePack,
			"top HUD keeps real focus candidates while its column overlay is"
			" non-interactive") && ok;
		menu.topMenu->deactivateControllerFocus();
	}
	else
	{
		ok = checkPack(menu.topMenu == nullptr && menu.columnMenu == nullptr,
			resourcePack,
			"resource profile omits the independent top and column surfaces")
			&& ok;
	}

	const MenuSurfaceCatalog::SurfacePolicy* loadingPolicy =
		MenuSurfaceCatalog::find(
			MenuSurfaceCatalog::SurfaceId::LoadingTextOverlay);
	ok = checkPack(loadingPolicy != nullptr
			&& loadingPolicy->modalKind
				== MenuSurfaceCatalog::ModalKind::Modal
			&& loadingPolicy->worldPointerPolicy
				== MenuSurfaceCatalog::WorldPointerPolicy::BlockAll
			&& loadingPolicy->worldSemanticPolicy
				== MenuSurfaceCatalog::WorldSemanticPolicy::Block
			&& loadingPolicy->focusPolicy
				== MenuSurfaceCatalog::FocusPolicy::None,
		resourcePack,
		"exclusive loading loop has an explicit modal no-focus contract")
		&& ok;

	menu.messageBox->visible = false;
	menu.systemNotice->visible = false;
	menu.timerMenu->stopTimer();
	menu.toolTip->hide();
	menu.npcInfoPanel->visible = false;
	menu.cancelControllerInteraction();
	return ok;
}

bool testSystemController(const ResourcePackExpectation& resourcePack)
{
	bool ok = true;
	System system;
	const bool complete = system.returnBtn != nullptr
		&& system.saveloadBtn != nullptr
		&& system.optionBtn != nullptr
		&& system.quitBtn != nullptr;
	if (!checkPack(complete, resourcePack,
		"system menu loads all four production actions"))
	{
		return false;
	}
	ok = checkPack(system.focusManager.getFocusedNodeId() == "return",
		resourcePack,
		"system menu keeps return as its semantic default") && ok;

	std::vector<std::pair<std::string, PElement>> visualOrder =
	{
		{ "return", system.returnBtn },
		{ "save-load", system.saveloadBtn },
		{ "options", system.optionBtn },
		{ "return-to-title", system.quitBtn }
	};
	std::stable_sort(
		visualOrder.begin(),
		visualOrder.end(),
		[](const auto& left, const auto& right)
		{
			const int leftCenter = left.second->rect.y + left.second->rect.h / 2;
			const int rightCenter = right.second->rect.y + right.second->rect.h / 2;
			return leftCenter < rightCenter;
		});
	for (std::size_t index = 0; index < visualOrder.size(); index++)
	{
		system.focusManager.focusNode(visualOrder[index].first);
		const std::string& nextId =
			visualOrder[(index + 1) % visualOrder.size()].first;
		ok = checkPack(system.handleUIAction(UIAction::NavigateDown)
			&& system.focusManager.getFocusedNodeId() == nextId,
			resourcePack,
			"system down navigation follows the resource's visual order") && ok;

		system.focusManager.focusNode(visualOrder[index].first);
		const std::string& previousId = visualOrder[
			(index + visualOrder.size() - 1) % visualOrder.size()].first;
		ok = checkPack(system.handleUIAction(UIAction::NavigateUp)
			&& system.focusManager.getFocusedNodeId() == previousId,
			resourcePack,
			"system up navigation follows the resource's visual order") && ok;
	}

	System optionFocused(true);
	ok = checkPack(optionFocused.focusManager.getFocusedNodeId() == "options",
		resourcePack,
		"system options entry explicitly requests options focus") && ok;
	System cancelSystem;
	cancelSystem.setRunning(true);
	ok = checkPack(cancelSystem.handleUIAction(UIAction::Cancel)
		&& cancelSystem.result == erOK,
		resourcePack,
		"system cancel returns to the game") && ok;
	System titleSystem;
	titleSystem.setRunning(true);
	titleSystem.focusManager.focusNode("return-to-title");
	ok = checkPack(titleSystem.handleUIAction(UIAction::Confirm)
		&& titleSystem.result == erReturnToTitle,
		resourcePack,
		"system confirms return-to-title without opening a nested menu") && ok;
	return ok;
}

bool testOptionController(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	bool ok = true;
	if (gameManager.controller != nullptr
		&& (gameManager.controller->joystickPanel == nullptr
			|| gameManager.controller->skillPanel == nullptr))
	{
		gameManager.controller->init();
	}
	ScopedGameInputRegistration inputRegistration(false, false);
	const bool touchControlsCreated = gameManager.controller != nullptr
		&& gameManager.controller->joystickPanel != nullptr
		&& gameManager.controller->skillPanel != nullptr;
	if (!checkPack(touchControlsCreated, resourcePack,
		"game controller creates touch controls on every platform"))
	{
		return false;
	}
#if defined(__MOBILE__)
	ok = checkPack(gameManager.controller->areTouchControlsVisible()
		&& gameManager.controller->joystickPanel->visible
		&& gameManager.controller->skillPanel->visible,
		resourcePack,
		"mobile builds show touch controls by default") && ok;
#else
	ok = checkPack(!gameManager.controller->areTouchControlsVisible()
		&& !gameManager.controller->joystickPanel->visible
		&& !gameManager.controller->skillPanel->visible,
		resourcePack,
		"desktop builds keep available touch controls hidden by default") && ok;
#endif
	const std::string previousAssetsCollectionRoot =
		File::getAssetsCollectionRoot();
	Option option;
	const bool complete = option.rtnBtn != nullptr
		&& option.music != nullptr && option.music->slideBtn != nullptr
		&& option.sound != nullptr && option.sound->slideBtn != nullptr
		&& option.speed != nullptr && option.speed->slideBtn != nullptr
		&& option.musicCB != nullptr && option.soundCB != nullptr
		&& option.speedCB != nullptr && option.playerAlpha != nullptr
		&& option.dyLoad != nullptr && option.shadow != nullptr;
	if (!checkPack(complete, resourcePack,
		"option menu loads its production controls"))
	{
		return false;
	}

	const bool slidersUsable = option.music->max > option.music->min
		&& option.sound->max > option.sound->min
		&& option.speed->max > option.speed->min
		&& option.music->slideBtn->rect.w > 0
		&& option.music->slideBtn->rect.h > 0
		&& option.sound->slideBtn->rect.w > 0
		&& option.sound->slideBtn->rect.h > 0
		&& option.speed->slideBtn->rect.w > 0
		&& option.speed->slideBtn->rect.h > 0;
	if (!checkPack(slidersUsable, resourcePack,
		"option menu loads usable production slider ranges and handles"))
	{
		return false;
	}
	ok = checkPack(option.music->rect.w == resourcePack.optionMusicWidth
		&& option.speed->max == resourcePack.optionSpeedMaximum,
		resourcePack,
		"option menu exposes resource-specific production geometry and range") && ok;

	std::vector<std::string> focusOrder = { "music", "sound", "speed" };
	const bool exposesExtendedCheckBoxes =
		std::string(resourcePack.id) == "JXQY2";
	if (exposesExtendedCheckBoxes)
	{
		focusOrder.push_back("player-alpha");
		focusOrder.push_back("dynamic-loading");
	}
	if (!checkPack(option.touchControlsButton != nullptr
		&& option.touchControlsButton->visible
		&& option.touchControlsButton->activated,
		resourcePack,
		"option creates an enabled cross-platform touch-controls setting"))
	{
		return false;
	}
	focusOrder.push_back("touch-controls");
	if (!checkPack(option.cheatSettingsButton != nullptr
		&& option.cheatSettingsButton->visible
		&& option.cheatSettingsButton->activated
		&& option.cheatSettingsButton->rect.y
			== option.touchControlsButton->rect.y,
		resourcePack,
		"option creates a touch-accessible cheat setting beside touch controls"))
	{
		return false;
	}
	focusOrder.push_back("cheat-settings");

	ok = checkPack(option.focusManager.getFocusedNodeId() == "music",
		resourcePack,
		"option defaults to the first usable production slider") && ok;
	for (std::size_t index = 1; index <= focusOrder.size(); index++)
	{
		ok = checkPack(option.handleUIAction(UIAction::NavigateDown)
			&& option.focusManager.getFocusedNodeId()
				== focusOrder[index % focusOrder.size()],
			resourcePack,
			"option follows and wraps its enabled vertical setting order") && ok;
	}
	ok = checkPack(option.handleUIAction(UIAction::NavigateUp)
		&& option.focusManager.getFocusedNodeId() == focusOrder.back(),
		resourcePack,
		"option wraps upward to the last enabled setting") && ok;
	ok = checkPack(!option.focusManager.focusNode("shadow"),
		resourcePack,
		"option omits the production-disabled shadow setting from focus") && ok;
	if (exposesExtendedCheckBoxes)
	{
		ok = checkPack(option.focusManager.focusNode("player-alpha")
			&& option.focusManager.focusNode("dynamic-loading"),
			resourcePack,
			"option exposes the resource's enabled checkbox settings") && ok;
	}
	else
	{
		ok = checkPack(!option.playerAlpha->activated
			&& !option.dyLoad->activated
			&& !option.focusManager.focusNode("player-alpha")
			&& !option.focusManager.focusNode("dynamic-loading"),
			resourcePack,
			"option skips checkbox settings without a drawable control or label") && ok;
	}
	option.focusManager.focusDefault();

	const bool touchControlsVisibleBefore =
		gameManager.controller->areTouchControlsVisible();
	const bool touchControlsFocused =
		option.focusManager.focusNode("touch-controls");
	const bool firstTouchToggleHandled =
		touchControlsFocused
		&& option.handleUIAction(UIAction::Confirm);
	const bool firstTouchToggleDeferred =
		gameManager.controller->areTouchControlsVisible()
			== touchControlsVisibleBefore
		&& !Element::isRawPointerInputBlocked();
	Element::dispatchFrameGlobalInput(Engine::getInstance());
	const bool touchControlsToggled =
		gameManager.controller->areTouchControlsVisible()
			!= touchControlsVisibleBefore
		&& Element::isRawPointerInputBlocked();
	Element::dispatchFrameGlobalInput(Engine::getInstance());
	const bool firstTouchToggleGateDrained =
		!Element::isRawPointerInputBlocked();
	const bool secondTouchToggleHandled =
		firstTouchToggleHandled
		&& option.handleUIAction(UIAction::Confirm);
	const bool secondTouchToggleDeferred =
		gameManager.controller->areTouchControlsVisible()
			!= touchControlsVisibleBefore
		&& !Element::isRawPointerInputBlocked();
	Element::dispatchFrameGlobalInput(Engine::getInstance());
	const bool touchControlsRestored =
		gameManager.controller->areTouchControlsVisible()
			== touchControlsVisibleBefore
		&& Element::isRawPointerInputBlocked();
	Element::dispatchFrameGlobalInput(Engine::getInstance());
	const bool secondTouchToggleGateDrained =
		!Element::isRawPointerInputBlocked();
	ok = checkPack(touchControlsFocused
		&& firstTouchToggleHandled
		&& firstTouchToggleDeferred
		&& touchControlsToggled
		&& firstTouchToggleGateDrained
		&& secondTouchToggleHandled
		&& secondTouchToggleDeferred
		&& touchControlsRestored
		&& secondTouchToggleGateDrained,
		resourcePack,
		"option confirmation toggles and restores cross-platform touch controls through the pre-pointer transaction") && ok;
	option.focusManager.focusDefault();

	const bool cheatPanelOpened =
		option.focusManager.focusNode("cheat-settings")
		&& option.handleUIAction(UIAction::Confirm)
		&& option.focusManager.getFocusedNodeId() == "cheat-mode"
		&& !option.music->visible
		&& !option.focusManager.focusNode("music");
	const bool cheatPanelClosed = cheatPanelOpened
		&& option.handleUIAction(UIAction::Cancel)
		&& option.focusManager.getFocusedNodeId() == "cheat-settings"
		&& option.music->visible;
	ok = checkPack(cheatPanelOpened && cheatPanelClosed,
		resourcePack,
		"option cheat panel opens as an independent focus scope and returns to its entry") && ok;
	option.focusManager.focusDefault();

	const float originalMusicVolume = Config::getMusicVolume();
	const float originalSoundVolume = Config::getSoundVolume();
	const float originalGameSpeed = Config::getGameSpeed();
	const bool originalPlayerAlpha = Config::playerAlpha;
	const bool originalDynamicLoading = Config::loadAsync;
	const std::filesystem::path temporaryRoot =
		makeUniqueTestDirectory("jxqy_gamepad_option_test");
	std::error_code errorCode;
	std::filesystem::remove_all(temporaryRoot, errorCode);
	errorCode.clear();
	std::filesystem::create_directories(temporaryRoot, errorCode);
	if (!checkPack(!errorCode, resourcePack,
		"option test creates an isolated configuration directory"))
	{
		return false;
	}
	const std::filesystem::path temporaryAssetsRoot =
		temporaryRoot / "assets";
	std::filesystem::create_directories(
		temporaryAssetsRoot, errorCode);
	File::setAssetsCollectionRoot(
		temporaryAssetsRoot.generic_string());

	auto testSlider = [&](const std::string& focusId,
		const std::shared_ptr<Scrollbar>& scrollbar,
		const std::function<float()>& currentValue,
		const std::function<float(const Scrollbar&)>& expectedValue,
		const std::string& label)
	{
		const int step = scrollbar->lineSize > 0 ? scrollbar->lineSize : 1;
		const int middle = scrollbar->min
			+ (scrollbar->max - scrollbar->min) / 2;
		scrollbar->setPosition(middle);
		const int expectedRight = std::min(scrollbar->max, middle + step);
		bool sliderOk = option.focusManager.focusNode(focusId)
			&& option.handleUIAction(UIAction::NavigateRight)
			&& scrollbar->position == expectedRight
			&& nearlyEqual(currentValue(), expectedValue(*scrollbar));
		sliderOk = option.handleUIAction(UIAction::NavigateLeft)
			&& scrollbar->position == middle
			&& nearlyEqual(currentValue(), expectedValue(*scrollbar))
			&& sliderOk;
		ok = checkPack(sliderOk, resourcePack,
			"option adjusts and applies the " + label
				+ " slider with left and right") && ok;
	};

	testSlider(
		"music",
		option.music,
		[]() { return Config::getMusicVolume(); },
		[](const Scrollbar& scrollbar)
		{
			return expectedScrollbarVolume(scrollbar);
		},
		"music volume");
	testSlider(
		"sound",
		option.sound,
		[]() { return Config::getSoundVolume(); },
		[](const Scrollbar& scrollbar)
		{
			return expectedScrollbarVolume(scrollbar);
		},
		"sound volume");
	testSlider(
		"speed",
		option.speed,
		[]() { return Config::getGameSpeed(); },
		[](const Scrollbar& scrollbar)
		{
			return expectedSpeedAtPosition(scrollbar);
		},
		"game speed");

	option.focusManager.focusNode("music");
	option.musicCB->checked = false;
	ok = checkPack(option.handleUIAction(UIAction::Confirm)
		&& option.musicCB->checked
		&& option.music->position == option.music->min
		&& nearlyEqual(Config::getMusicVolume(), 0.0f)
		&& option.handleUIAction(UIAction::Confirm)
		&& !option.musicCB->checked
		&& option.music->position == option.music->max
		&& nearlyEqual(Config::getMusicVolume(), 1.0f),
		resourcePack,
		"option confirmation toggles production music mute") && ok;

	option.focusManager.focusNode("sound");
	option.soundCB->checked = false;
	ok = checkPack(option.handleUIAction(UIAction::Confirm)
		&& option.soundCB->checked
		&& option.sound->position == option.sound->min
		&& nearlyEqual(Config::getSoundVolume(), 0.0f)
		&& option.handleUIAction(UIAction::Confirm)
		&& !option.soundCB->checked
		&& option.sound->position == option.sound->max
		&& nearlyEqual(Config::getSoundVolume(), 1.0f),
		resourcePack,
		"option confirmation toggles production sound mute") && ok;

	option.speed->setPosition(option.speed->min);
	const int defaultSpeedPosition = expectedDefaultSpeedPosition(*option.speed);
	ok = checkPack(option.focusManager.focusNode("speed")
		&& option.handleUIAction(UIAction::Confirm)
		&& option.speed->position == defaultSpeedPosition
		&& nearlyEqual(
			Config::getGameSpeed(), expectedSpeedAtPosition(*option.speed)),
		resourcePack,
		"option confirmation resets production game speed") && ok;

	if (exposesExtendedCheckBoxes)
	{
		const bool playerAlphaBefore = Config::playerAlpha;
		const bool playerAlphaCheckedBefore = option.playerAlpha->checked;
		ok = checkPack(option.focusManager.focusNode("player-alpha")
			&& option.handleUIAction(UIAction::Confirm)
			&& option.playerAlpha->checked != playerAlphaCheckedBefore
			&& Config::playerAlpha != playerAlphaBefore
			&& Config::playerAlpha == !option.playerAlpha->checked,
			resourcePack,
			"option confirmation applies the player-alpha checkbox") && ok;

		const bool dynamicLoadingBefore = Config::loadAsync;
		const bool dynamicLoadingCheckedBefore = option.dyLoad->checked;
		ok = checkPack(option.focusManager.focusNode("dynamic-loading")
			&& option.handleUIAction(UIAction::Confirm)
			&& option.dyLoad->checked != dynamicLoadingCheckedBefore
			&& Config::loadAsync != dynamicLoadingBefore
			&& Config::loadAsync == !option.dyLoad->checked,
			resourcePack,
			"option confirmation applies the dynamic-loading checkbox") && ok;
	}

	option.setRunning(true);
	ok = checkPack(option.handleUIAction(UIAction::Cancel)
		&& option.result == erOK,
		resourcePack,
		"option cancel returns to the system menu") && ok;

	Config::setMusicVolume(originalMusicVolume);
	Config::setSoundVolume(originalSoundVolume);
	Config::setGameSpeed(originalGameSpeed);
	Config::playerAlpha = originalPlayerAlpha;
	Config::loadAsync = originalDynamicLoading;
	File::setAssetsCollectionRoot(previousAssetsCollectionRoot);
	std::filesystem::remove_all(temporaryRoot, errorCode);
	return ok;
}

bool testSaveLoadController(
	const ResourcePackExpectation& resourcePack,
	ResourceManager& resourceManager,
	GameManager& gameManager)
{
	bool ok = true;
	SaveLoad combined(true, true);
	SaveLoad loadOnly(false, true);
	SaveLoad saveOnly(true, false);
	const bool complete = combined.listBox != nullptr
		&& combined.loadBtn != nullptr
		&& combined.saveBtn != nullptr
		&& combined.exitBtn != nullptr
		&& loadOnly.listBox != nullptr
		&& loadOnly.loadBtn != nullptr
		&& loadOnly.exitBtn != nullptr
		&& saveOnly.listBox != nullptr
		&& saveOnly.saveBtn != nullptr
		&& saveOnly.exitBtn != nullptr;
	if (!checkPack(complete, resourcePack,
		"save-load menu loads its production controls"))
	{
		return false;
	}
	ok = checkPack(
		static_cast<int>(combined.listBox->itemButton.size())
			== resourcePack.saveSlotCount,
		resourcePack,
		"save-load exposes the resource's configured slot count") && ok;
	ok = checkPack(
		GamepadEssentialUITestAccess::focusedSaveLoadControl(combined)
			== combined.loadBtn
		&& combined.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedSaveLoadControl(combined)
			== combined.saveBtn
		&& combined.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedSaveLoadControl(combined)
			== combined.exitBtn
		&& combined.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedSaveLoadControl(combined)
			== combined.loadBtn
		&& combined.handleUIAction(UIAction::NavigateLeft)
		&& GamepadEssentialUITestAccess::focusedSaveLoadControl(combined)
			== combined.exitBtn,
		resourcePack,
		"save-load cycles load, save, and return as horizontal actions") && ok;

	const std::filesystem::path temporaryRoot =
		makeUniqueTestDirectory("jxqy_gamepad_save_load_test");
	std::error_code errorCode;
	std::filesystem::remove_all(temporaryRoot, errorCode);
	std::filesystem::create_directories(temporaryRoot, errorCode);
	File::setActiveResourceRoot(temporaryRoot.generic_string());
	File::setCommonResourceRoot("");
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots({});
	File::setActiveSaveNamespace(
		std::string("gamepad-save-load-") + resourcePack.id);

	if (GamepadEssentialUITestAccess::focusedSaveLoadControl(combined)
		!= combined.loadBtn)
	{
		combined.handleUIAction(UIAction::NavigateRight);
	}
	combined.index = 0;
	combined.listBox->setSelectedIndex(0);
	ok = checkPack(combined.handleUIAction(UIAction::NavigateUp)
		&& combined.index == resourcePack.saveSlotCount - 1
		&& combined.handleUIAction(UIAction::NavigateDown)
		&& combined.index == 0,
		resourcePack,
		"save-load wraps through real save slots") && ok;

	loadOnly.setRunning(true);
	ok = checkPack(
		GamepadEssentialUITestAccess::focusedSaveLoadControl(loadOnly)
			== loadOnly.loadBtn
		&& !loadOnly.saveBtn->visible
		&& loadOnly.handleUIAction(UIAction::Confirm)
		&& loadOnly.result == erNone,
		resourcePack,
		"load-only mode rejects confirmation before a slot is selected") && ok;
#if !defined(__ANDROID__) && !defined(__APPLE__)
	loadOnly.index = 0;
	loadOnly.listBox->setSelectedIndex(0);
	ok = checkPack(loadOnly.handleUIAction(UIAction::Confirm)
		&& loadOnly.result == erNone,
		resourcePack,
		"load-only mode rejects an empty temporary slot") && ok;
	ok = checkPack(writeSaveMarker(1),
		resourcePack,
		"load-only test creates an isolated save marker") && ok;
	ok = checkPack(loadOnly.handleUIAction(UIAction::Confirm)
		&& loadOnly.result == erLoad,
		resourcePack,
		"load-only mode accepts an existing isolated slot") && ok;
#endif

	saveOnly.index = 0;
	saveOnly.listBox->setSelectedIndex(0);
	saveOnly.setRunning(true);
	gameManager.inEvent = true;
	ok = checkPack(
		GamepadEssentialUITestAccess::focusedSaveLoadControl(saveOnly)
			== saveOnly.saveBtn
		&& !saveOnly.loadBtn->visible
		&& saveOnly.handleUIAction(UIAction::Confirm)
		&& saveOnly.result == erNone,
		resourcePack,
		"save-only mode keeps the event save gate") && ok;
	gameManager.inEvent = false;
	gameManager.global.data.saveDisabled = true;
	ok = checkPack(saveOnly.handleUIAction(UIAction::Confirm)
		&& saveOnly.result == erNone,
		resourcePack,
		"save-only mode keeps the location save gate") && ok;
	gameManager.global.data.saveDisabled = false;
	ok = checkPack(saveOnly.handleUIAction(UIAction::Confirm)
		&& saveOnly.result == erSave,
		resourcePack,
		"save-only mode returns a save request without writing the save") && ok;

	if (GamepadEssentialUITestAccess::focusedSaveLoadControl(combined)
		!= combined.exitBtn)
	{
		combined.handleUIAction(UIAction::NavigateLeft);
	}
	combined.result = erNone;
	combined.setRunning(true);
	ok = checkPack(
		GamepadEssentialUITestAccess::focusedSaveLoadControl(combined)
			== combined.exitBtn
		&& combined.handleUIAction(UIAction::Confirm)
		&& combined.result == erOK,
		resourcePack,
		"save-load confirms its focused return button") && ok;
	combined.result = erNone;
	combined.setRunning(true);
	ok = checkPack(combined.handleUIAction(UIAction::Cancel)
		&& combined.result == erOK,
		resourcePack,
		"save-load cancel returns without selecting an action") && ok;

	ok = checkPack(resourceManager.setActiveResourcePackById(resourcePack.id),
		resourcePack,
		"save-load test restores the production resource roots") && ok;
	std::filesystem::remove_all(temporaryRoot, errorCode);
	return ok;
}

bool testYesNoAndDialogController(const ResourcePackExpectation& resourcePack)
{
	bool ok = true;
	INIReader dialogLabelDefinition("ini\\ui\\dialog\\label.ini");
	ok = checkPack(dialogLabelDefinition.ParseError() == 0
		&& dialogLabelDefinition.GetInteger("Init", "LineCount", 0)
			== resourcePack.dialogLineCount,
		resourcePack,
		"dialog production resource declares its explicit row count") && ok;
	if (std::string(resourcePack.id) == "YYCS")
	{
		ok = checkPack(
			dialogLabelDefinition.GetInteger("Init", "Font", 0) == 18 &&
			dialogLabelDefinition.GetInteger(
				"Init", "CharactersPerLine", 0) == 17 &&
			dialogLabelDefinition.GetInteger("Init", "LineHeight", 0) == 22,
			resourcePack,
			"dialog production resource declares the enlarged non-overlapping text grid") && ok;
	}
	YesNo yesNo("Controller confirmation");
	if (!checkPack(yesNo.yes != nullptr && yesNo.no != nullptr
		&& yesNo.label != nullptr,
		resourcePack,
		"yes-no loads its production controls"))
	{
		return false;
	}
	const int yesCenterX = yesNo.yes->rect.x + yesNo.yes->rect.w / 2;
	const int yesCenterY = yesNo.yes->rect.y + yesNo.yes->rect.h / 2;
	const int noCenterX = yesNo.no->rect.x + yesNo.no->rect.w / 2;
	const int noCenterY = yesNo.no->rect.y + yesNo.no->rect.h / 2;
	const int deltaX = yesCenterX - noCenterX;
	const int deltaY = yesCenterY - noCenterY;
	const UIAction navigateToYes =
		std::abs(deltaX) >= std::abs(deltaY)
			? (deltaX < 0
				? UIAction::NavigateLeft : UIAction::NavigateRight)
			: (deltaY < 0
				? UIAction::NavigateUp : UIAction::NavigateDown);
	ok = checkPack(deltaX != 0 || deltaY != 0,
		resourcePack,
		"yes-no production controls have distinct visual positions") && ok;
	ok = checkPack(
		GamepadEssentialUITestAccess::focusedYesNoControl(yesNo) == yesNo.no
		&& yesNo.handleUIAction(navigateToYes)
		&& GamepadEssentialUITestAccess::focusedYesNoControl(yesNo)
			== yesNo.yes,
		resourcePack,
		"yes-no defaults to no and navigates toward the visually positioned yes button") && ok;
	yesNo.setRunning(true);
	ok = checkPack(yesNo.handleUIAction(UIAction::Confirm)
		&& yesNo.result == erOK,
		resourcePack,
		"yes-no confirms the focused affirmative action") && ok;
	YesNo cancelYesNo("Controller cancellation");
	cancelYesNo.setRunning(true);
	ok = checkPack(cancelYesNo.handleUIAction(UIAction::Cancel)
		&& cancelYesNo.result == erExit,
		resourcePack,
		"yes-no maps cancel to the negative action") && ok;

	Dialog dialog;
	auto dialogLabel = dialog.getComponentByName<TalkLabel>("label");
	if (!checkPack(dialogLabel != nullptr && dialogLabel->fontSize > 0,
		resourcePack,
		"dialog loads its production talk label"))
	{
		return false;
	}
	const int dialogCharactersPerLine = std::max(
		1, dialogLabel->rect.w / dialogLabel->fontSize);
	const int expectedDialogPageCharacters =
		dialogCharactersPerLine * resourcePack.dialogLineCount;
	const int productionDialogHeight = dialogLabel->rect.h;
	dialogLabel->rect.h = dialogLabel->fontSize;
	const std::vector<TalkString> productionDialogPages =
		dialogLabel->splitTalkString(std::string(
			static_cast<std::size_t>(expectedDialogPageCharacters + 1), 'D'));
	dialogLabel->rect.h = productionDialogHeight;
	ok = checkPack(productionDialogPages.size() == 2
		&& static_cast<int>(productionDialogPages.front().talkChar.size())
			== expectedDialogPageCharacters,
		resourcePack,
		"dialog pagination keeps the production resource's explicit row count"
			+ std::string(" (expected first page ")
			+ std::to_string(expectedDialogPageCharacters)
			+ ", actual "
			+ std::to_string(productionDialogPages.empty()
				? 0 : productionDialogPages.front().talkChar.size())
			+ ", pages " + std::to_string(productionDialogPages.size()) + ")") && ok;

	if (std::string(resourcePack.id) == "YYCS")
	{
		const std::vector<TalkString> exactPage =
			dialogLabel->splitTalkString(std::string(51, 'D'));
		const std::vector<TalkString> overflowPage =
			dialogLabel->splitTalkString(std::string(52, 'D'));
		const std::vector<TalkString> explicitRows =
			dialogLabel->splitTalkString(
				u8"甲<enter><enter><color=red>乙\r\n丙");
		ok = checkPack(
			exactPage.size() == 1 && exactPage[0].talkChar.size() == 51 &&
			overflowPage.size() == 2 &&
			overflowPage[0].talkChar.size() == 51 &&
			overflowPage[1].talkChar.size() == 1,
			resourcePack,
			"dialog exact 51-character pages do not append an empty page") && ok;
		ok = checkPack(
			explicitRows.size() == 2 &&
			explicitRows[0].talkChar.size() == 2 &&
			explicitRows[0].talkChar[0].row == 0 &&
			explicitRows[0].talkChar[1].row == 2 &&
			explicitRows[0].talkChar[1].color == 0xFFFF0000 &&
			explicitRows[1].talkChar.size() == 1 &&
			explicitRows[1].talkChar[0].color == 0xFFFF0000,
			resourcePack,
			"dialog explicit breaks preserve blank rows and color across page boundaries") && ok;
	}

	if (std::string(resourcePack.id) == "JXQY2")
	{
		TalkLabel defaultDialogLabel;
		const std::vector<TalkString> defaultDialogPages =
			defaultDialogLabel.splitTalkString(std::string(19 * 3 + 1, 'D'));
		ok = checkPack(defaultDialogPages.size() == 2
			&& defaultDialogPages.front().talkChar.size() == 19 * 3,
			resourcePack,
			"dialog fallback keeps the JXQY2 three-line default") && ok;

		TalkLabel legacyGeometryDialogLabel;
		legacyGeometryDialogLabel.rect.w = 456;
		legacyGeometryDialogLabel.rect.h = 54;
		legacyGeometryDialogLabel.fontSize = 16;
		const std::vector<TalkString> legacyGeometryDialogPages =
			legacyGeometryDialogLabel.splitTalkString(std::string(28 * 3 + 1, 'D'));
		ok = checkPack(legacyGeometryDialogPages.size() == 2
			&& legacyGeometryDialogPages.front().talkChar.size() == 28 * 3,
			resourcePack,
			"dialog keeps geometry-driven pagination for legacy INI files") && ok;
	}
	dialog.setTalkStr("Controller dialog page");
	dialog.setRunning(true);
	ok = checkPack(dialog.handleUIAction(UIAction::Cancel)
		&& dialog.result == erNone,
		resourcePack,
		"dialog consumes cancel without skipping script-owned state") && ok;
	for (int attempt = 0; attempt < 4 && dialog.result == erNone; attempt++)
	{
		ok = checkPack(dialog.handleUIAction(UIAction::Confirm),
			resourcePack,
			"dialog consumes semantic confirmation") && ok;
	}
	ok = checkPack(dialog.result == erOK,
		resourcePack,
		"dialog completes after revealing and advancing its production page") && ok;
	return ok;
}

bool testChooseController(const ResourcePackExpectation& resourcePack)
{
	bool ok = true;
	ChooseMenu ordinary;
	if (!checkPack(
		ordinary.getComponentByName<Label>("messageLabel") != nullptr
			&& ordinary.getComponentByName<ChooseTextButton>("selectA") != nullptr
			&& ordinary.getComponentByName<ChooseTextButton>("selectB") != nullptr,
		resourcePack,
		"choose loads its production controls"))
	{
		return false;
	}
	ok = checkPack(GamepadEssentialUITestAccess::prepareOrdinaryChoice(
		ordinary,
		"Choose one",
		{ "First", "Second" },
		{ true, true })
		&& GamepadEssentialUITestAccess::focusedChoice(ordinary) == "choice-0",
		resourcePack,
		"choose prepares a non-blocking production session on the first option") && ok;
	ok = checkPack(ordinary.handleUIAction(UIAction::Cancel)
		&& GamepadEssentialUITestAccess::choiceSessionActive(ordinary),
		resourcePack,
		"choose consumes cancel without cancelling a script choice") && ok;
	const bool ordinaryMoved = ordinary.handleUIAction(UIAction::NavigateDown)
		&& GamepadEssentialUITestAccess::focusedChoice(ordinary) == "choice-1";
	ok = checkPack(ordinaryMoved,
		resourcePack,
		"choose navigates to the next production option") && ok;
	ok = checkPack(ordinary.handleUIAction(UIAction::Confirm),
		resourcePack,
		"choose consumes confirmation on the focused option") && ok;
	ok = checkPack(ordinary.getSelection() == 1
		&& !GamepadEssentialUITestAccess::choiceSessionActive(ordinary),
		resourcePack,
		"choose returns the original selected index and ends the session") && ok;

	ChooseMenu keyboard;
	ok = checkPack(GamepadEssentialUITestAccess::prepareOrdinaryChoice(
		keyboard,
		"Keyboard choice",
		{ "First", "Second", "Third" },
		{ true, true, true })
		&& GamepadEssentialUITestAccess::focusedChoice(keyboard) == "choice-0"
		&& GamepadEssentialUITestAccess::currentPageIndex(keyboard) == 0,
		resourcePack,
		"choose keyboard production fixture starts on its first concrete option") && ok;
	ok = checkPack(
		GamepadEssentialUITestAccess::dispatchChoiceKeyboardFrame(
			keyboard, KEY_UP, true)
			&& GamepadEssentialUITestAccess::focusedChoice(keyboard)
				== "choice-0"
			&& GamepadEssentialUITestAccess::
				choiceNavigationIndicatorMatchesFocus(keyboard),
		resourcePack,
		"choose keyboard owns a boundary direction and keeps its concrete marker")
		&& ok;
	ok = checkPack(
		GamepadEssentialUITestAccess::dispatchChoiceKeyboardFrame(
			keyboard, KEY_DOWN, true)
			&& GamepadEssentialUITestAccess::focusedChoice(keyboard)
				== "choice-1"
			&& GamepadEssentialUITestAccess::currentPageIndex(keyboard) == 0
			&& GamepadEssentialUITestAccess::
				choiceNavigationIndicatorMatchesFocus(keyboard),
		resourcePack,
		"choose keyboard down moves and visibly marks one concrete option"
		" without paging across the engine synthetic mouse refresh") && ok;
	ok = checkPack(
		GamepadEssentialUITestAccess::dispatchChoicePointerFrame(
			keyboard, false)
			&& GamepadEssentialUITestAccess::focusedChoice(keyboard)
				== "choice-1"
			&& !GamepadEssentialUITestAccess::
				choiceNavigationIndicatorVisible(keyboard),
		resourcePack,
		"choose real pointer motion hides only the keyboard navigation marker")
		&& ok;
	ok = checkPack(
		GamepadEssentialUITestAccess::dispatchChoiceKeyboardFrame(
			keyboard, KEY_UP)
			&& GamepadEssentialUITestAccess::focusedChoice(keyboard)
				== "choice-0"
			&& GamepadEssentialUITestAccess::
				choiceNavigationIndicatorMatchesFocus(keyboard)
			&& GamepadEssentialUITestAccess::dispatchChoiceKeyboardFrame(
				keyboard, KEY_DOWN)
			&& GamepadEssentialUITestAccess::focusedChoice(keyboard)
				== "choice-1"
			&& GamepadEssentialUITestAccess::
				choiceNavigationIndicatorMatchesFocus(keyboard),
		resourcePack,
		"choose keyboard restores its concrete marker after real pointer input")
		&& ok;
	int previousWindowWidth = 0;
	int previousWindowHeight = 0;
	Engine::getInstance()->getWindowSize(
		previousWindowWidth, previousWindowHeight);
	ok = checkPack(
		GamepadEssentialUITestAccess::dispatchChoiceResizeFrame(
			keyboard, 1024, 720)
			&& GamepadEssentialUITestAccess::focusedChoice(keyboard)
				== "choice-1"
			&& GamepadEssentialUITestAccess::
				choiceNavigationIndicatorMatchesFocus(keyboard),
		resourcePack,
		"choose resize preserves its concrete keyboard focus and non-gamepad"
		" navigation marker") && ok;
	ok = checkPack(
		GamepadEssentialUITestAccess::dispatchChoiceResizeFrame(
			keyboard, previousWindowWidth, previousWindowHeight)
			&& GamepadEssentialUITestAccess::focusedChoice(keyboard)
				== "choice-1"
			&& GamepadEssentialUITestAccess::
				choiceNavigationIndicatorMatchesFocus(keyboard),
		resourcePack,
		"choose resize restoration keeps its concrete keyboard focus") && ok;
	ok = checkPack(
		GamepadEssentialUITestAccess::dispatchChoiceKeyboardFrame(
			keyboard, KEY_RETURN)
			&& keyboard.getSelection() == 1
			&& !GamepadEssentialUITestAccess::choiceSessionActive(keyboard),
		resourcePack,
		"choose keyboard confirms the focused concrete option") && ok;

	ChooseMenu sparse;
	ok = checkPack(GamepadEssentialUITestAccess::prepareOrdinaryChoice(
		sparse,
		"Sparse choices",
		{ "Hidden", "Visible one", "", "Visible three" },
		{ false, true, true, true })
		&& GamepadEssentialUITestAccess::focusedChoice(sparse) == "choice-1",
		resourcePack,
		"choose starts a sparse choice on the first visible original index") && ok;
	ok = checkPack(sparse.handleUIAction(UIAction::NavigateDown)
		&& GamepadEssentialUITestAccess::focusedChoice(sparse) == "choice-3",
		resourcePack,
		"choose skips hidden and empty choices while navigating") && ok;
	ok = checkPack(sparse.handleUIAction(UIAction::Confirm)
		&& sparse.getSelection() == 3,
		resourcePack,
		"choose returns the sparse option's uncompressed script index") && ok;

	const std::vector<std::string> longOptions = makeLongChoiceOptions(48);
	const std::vector<bool> visibleOptions(longOptions.size(), true);
	ChooseMenu choosePlus;
	ok = checkPack(GamepadEssentialUITestAccess::prepareChoosePlus(
		choosePlus,
		"Paginated ChoosePlus",
		longOptions,
		visibleOptions)
		&& GamepadEssentialUITestAccess::currentPageCount(choosePlus) > 1,
		resourcePack,
		"ChoosePlus paginates long production choices") && ok;
	const std::string firstPageFocus =
		GamepadEssentialUITestAccess::focusedChoice(choosePlus);
	ok = checkPack(GamepadEssentialUITestAccess::focusChoice(
			choosePlus, "next-page")
		&& GamepadEssentialUITestAccess::dispatchChoiceKeyboardFrame(
			choosePlus, KEY_RETURN, true)
		&& GamepadEssentialUITestAccess::currentPageIndex(choosePlus) == 1
		&& GamepadEssentialUITestAccess::focusedChoice(choosePlus) != firstPageFocus
		&& GamepadEssentialUITestAccess::
			choiceNavigationIndicatorMatchesFocus(choosePlus)
		&& choosePlus.handleUIAction(UIAction::PanelPrevious)
		&& GamepadEssentialUITestAccess::currentPageIndex(choosePlus) == 0
		&& choosePlus.handleUIAction(UIAction::PanelNext)
		&& GamepadEssentialUITestAccess::currentPageIndex(choosePlus) == 1
		&& choosePlus.handleUIAction(UIAction::PanelPrevious)
		&& GamepadEssentialUITestAccess::currentPageIndex(choosePlus) == 0,
		resourcePack,
		"ChoosePlus keyboard confirms its page control and preserves panel"
		" navigation") && ok;

	ChooseMenu multiple;
	ok = checkPack(GamepadEssentialUITestAccess::prepareMultipleChoice(
		multiple,
		"Select two",
		longOptions,
		visibleOptions,
		2,
		2)
		&& GamepadEssentialUITestAccess::currentPageCount(multiple) > 2
		&& multiple.handleUIAction(UIAction::PanelNext)
		&& GamepadEssentialUITestAccess::currentPageIndex(multiple) == 1,
		resourcePack,
		"multiple choice reaches a middle production page") && ok;
	std::vector<std::string> pageChoiceIds =
		GamepadEssentialUITestAccess::currentChoiceIds(multiple);
	if (!checkPack(pageChoiceIds.size() >= 2, resourcePack,
		"multiple choice middle page exposes at least two choices"))
	{
		return false;
	}
	ok = checkPack(GamepadEssentialUITestAccess::focusChoice(
			multiple, pageChoiceIds[0])
		&& GamepadEssentialUITestAccess::dispatchChoiceKeyboardFrame(
			multiple, KEY_RETURN)
		&& multiple.getMultipleSelection().size() == 1
		&& GamepadEssentialUITestAccess::
			focusedChoiceIsSelectedAndNavigationHighlighted(multiple)
		&& GamepadEssentialUITestAccess::dispatchChoiceKeyboardFrame(
			multiple, KEY_RETURN, false, true)
		&& multiple.getMultipleSelection().size() == 1
		&& GamepadEssentialUITestAccess::
			focusedChoiceIsSelectedAndNavigationHighlighted(multiple),
		resourcePack,
		"multiple choice keyboard marks the selected navigation item and"
		" suppresses repeated confirmation") && ok;
	ok = checkPack(GamepadEssentialUITestAccess::focusChoice(
		multiple, pageChoiceIds[1])
		&& multiple.handleUIAction(UIAction::Confirm)
		&& multiple.getMultipleSelection().size() == 2,
		resourcePack,
		"multiple choice toggles the second selected option") && ok;
	ok = checkPack(GamepadEssentialUITestAccess::focusChoice(
		multiple, "previous-page")
		&& GamepadEssentialUITestAccess::focusChoice(multiple, "next-page")
		&& GamepadEssentialUITestAccess::focusChoice(multiple, "clear")
		&& GamepadEssentialUITestAccess::focusChoice(multiple, "confirm"),
		resourcePack,
		"multiple choice registers both footer rows after reaching the selection count") && ok;
	ok = checkPack(GamepadEssentialUITestAccess::focusChoice(
		multiple, "previous-page")
		&& multiple.handleUIAction(UIAction::NavigateRight)
		&& GamepadEssentialUITestAccess::focusedChoice(multiple) == "next-page",
		resourcePack,
		"multiple choice explicitly connects the pagination row horizontally") && ok;
	GamepadEssentialUITestAccess::focusChoice(multiple, "previous-page");
	ok = checkPack(multiple.handleUIAction(UIAction::NavigateDown)
		&& GamepadEssentialUITestAccess::focusedChoice(multiple) == "clear",
		resourcePack,
		"multiple choice explicitly connects previous-page down to clear") && ok;
	GamepadEssentialUITestAccess::focusChoice(multiple, "next-page");
	ok = checkPack(multiple.handleUIAction(UIAction::NavigateDown)
		&& GamepadEssentialUITestAccess::focusedChoice(multiple) == "confirm",
		resourcePack,
		"multiple choice explicitly connects next-page down to confirm") && ok;
	GamepadEssentialUITestAccess::focusChoice(multiple, "clear");
	ok = checkPack(multiple.handleUIAction(UIAction::NavigateUp)
		&& GamepadEssentialUITestAccess::focusedChoice(multiple) == "previous-page",
		resourcePack,
		"multiple choice explicitly connects clear up to previous-page") && ok;
	GamepadEssentialUITestAccess::focusChoice(multiple, "confirm");
	ok = checkPack(multiple.handleUIAction(UIAction::NavigateUp)
		&& GamepadEssentialUITestAccess::focusedChoice(multiple) == "next-page",
		resourcePack,
		"multiple choice explicitly connects confirm up to next-page") && ok;
	GamepadEssentialUITestAccess::focusChoice(multiple, "clear");
	ok = checkPack(multiple.handleUIAction(UIAction::Confirm)
		&& multiple.getMultipleSelection().empty()
		&& GamepadEssentialUITestAccess::choiceSessionActive(multiple),
		resourcePack,
		"multiple choice clear resets selections without ending the script choice") && ok;
	pageChoiceIds = GamepadEssentialUITestAccess::currentChoiceIds(multiple);
	ok = checkPack(GamepadEssentialUITestAccess::focusChoice(
		multiple, pageChoiceIds[0])
		&& multiple.handleUIAction(UIAction::Confirm)
		&& GamepadEssentialUITestAccess::focusChoice(multiple, pageChoiceIds[1])
		&& multiple.handleUIAction(UIAction::Confirm)
		&& multiple.getMultipleSelection().size() == 2,
		resourcePack,
		"multiple choice can select the required options again after clear") && ok;
	ok = checkPack(multiple.handleUIAction(UIAction::Cancel)
		&& GamepadEssentialUITestAccess::choiceSessionActive(multiple),
		resourcePack,
		"multiple choice consumes cancel without ending the session") && ok;
	GamepadEssentialUITestAccess::focusChoice(multiple, "confirm");
	ok = checkPack(multiple.handleUIAction(UIAction::Confirm)
		&& multiple.getMultipleSelection().size() == 2
		&& !GamepadEssentialUITestAccess::choiceSessionActive(multiple),
		resourcePack,
		"multiple choice confirms the required original selections") && ok;
	return ok;
}
}

bool runGamepadEssentialUITests()
{
	bool ok = true;
	ok = check(
		std::string(JxqyBuildVersion::ReleaseStage) == "Preview",
		"the current program exposes Preview as a display-only release"
		" stage") && ok;
	ok = check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"gamepad essential UI tests start without the SDL video subsystem") && ok;
	Engine::getInstance()->setWindowSize(800, 600);
	ok = check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"setting a headless logical viewport does not initialize SDL video") && ok;
	ok = testControllerPromptContracts() && ok;
	ok = testControllerHelpDismissalContract() && ok;

	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	const char* controllerHelpImageName = "controller-help.png";
	ok = check(std::filesystem::is_regular_file(
		assetsRoot / "common/image/ui/controller_help"
			/ controllerHelpImageName),
		"controller help diagram image exists in common resources") && ok;
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot("");
	File::setCommonResourceRoot("");
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots({});
	ResourceManager& resourceManager = ResourceManager::instance();
	if (!check(resourceManager.initialize(assetsRoot.generic_string()),
		"gamepad essential UI tests initialize the production resource collection"))
	{
		return false;
	}
	ok = testResourceSelectionController(resourceManager) && ok;
	ok = testPublishedModTitleResources(resourceManager) && ok;
	ok = testResourceSelectionPhysicalFirstAction() && ok;
	ok = testTitlePhysicalExitLink() && ok;
	ok = testActionOnlyModalSurfaces() && ok;

	for (const ResourcePackExpectation& resourcePack : ResourcePacks)
	{
		if (!checkPack(resourceManager.setActiveResourcePackById(resourcePack.id),
			resourcePack,
			"resource pack can be selected for essential UI tests"))
		{
			ok = false;
			continue;
		}
		const ResourceManifest& manifest = resourceManager.getActiveManifest();
		ok = checkPack(manifest.type == resourcePack.gameType,
			resourcePack,
			"resource pack exposes the expected game type") && ok;
		std::unique_ptr<char[]> controllerImageData;
		int controllerImageSize = 0;
		const std::string controllerHelpImagePath =
			"image/ui/controller_help/" + std::string(controllerHelpImageName);
		ok = checkPack(File::readFile(
			controllerHelpImagePath,
			controllerImageData,
			controllerImageSize)
			&& controllerImageData != nullptr && controllerImageSize > 0,
			resourcePack,
			"controller help diagram resolves through common resources") && ok;

		GameManager gameManager;
		configureGameManager(gameManager, manifest);
		ok = testTitleController(resourcePack) && ok;
		ok = testRunningModalSubtreeSurvivesConfigDrivenResize(resourcePack)
			&& ok;
		ok = testSystemController(resourcePack) && ok;
		ok = testOptionController(
			resourcePack, gameManager) && ok;
		ok = testSaveLoadController(
			resourcePack, resourceManager, gameManager) && ok;
		ok = testYesNoAndDialogController(resourcePack) && ok;
		ok = testChooseController(resourcePack) && ok;
		ok = testDisplayOnlyAndPassiveSurfaceContracts(
			resourcePack, gameManager) && ok;
	}

	ok = check(resourceManager.setActiveResourcePackById("JXQY2"),
		"gamepad essential UI tests restore the default resource pack") && ok;
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	ok = check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"gamepad essential UI tests finish without SDL video or a window") && ok;
	return ok;
}
