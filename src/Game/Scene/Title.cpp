#include "VideoPage.h"
#include "../../Engine/Engine.h"
#include "../../Engine/AspectFitLayout.h"
#include "TitleTeam.h"
#include "MainScene.h"
#include "Title.h"
#include "../Menu/ControllerPromptPresenter.h"
#include "../Menu/SaveLoad.h"
#include "../Config/Config.h"
#include "../../Engine/AudioDecodeSafety.h"
#include "../../Resource/ResourceManager.h"
#include "../../File/ResourcePathSafety.h"
#include "../../File/log.h"
#include "../Data/MediaPathResolver.h"
#include "../Data/NewYearPeriod.h"
#include "../GameTypes.h"

#include <algorithm>

namespace
{
constexpr int MaxTeamInfoBytes = 64 * 1024;
constexpr std::size_t MaximumPointerRippleCount = 4;
constexpr UTime LoadingFadeTime = 500;
constexpr UTime FailureNoticeTime = 15000;

bool shouldPlayStartupVideo(std::string& videoName)
{
	const std::string newYearPrefix = "newyear:";
	if (videoName.compare(0, newYearPrefix.size(), newYearPrefix) != 0)
	{
		return true;
	}

	videoName = videoName.substr(newYearPrefix.size());
	return !videoName.empty() && NewYearPeriod::containsCurrentLocalDate();
}

bool loadActiveTeamInfo(const std::string& fileName, std::string& teamInfoText)
{
	teamInfoText.clear();
	if (fileName.empty())
	{
		return false;
	}

	std::unique_ptr<char[]> data;
	int length = 0;
	if (!File::readActiveResourceFile(fileName, data, length, MaxTeamInfoBytes) ||
		data == nullptr)
	{
		GameLog::write("Title: can not load local team info %s\n", fileName.c_str());
		return false;
	}

	teamInfoText.assign(data.get(), static_cast<std::size_t>(length));
	if (teamInfoText.size() >= 3 &&
		static_cast<unsigned char>(teamInfoText[0]) == 0xEF &&
		static_cast<unsigned char>(teamInfoText[1]) == 0xBB &&
		static_cast<unsigned char>(teamInfoText[2]) == 0xBF)
	{
		teamInfoText.erase(0, 3);
	}
	if (teamInfoText.find('\0') != std::string::npos ||
		!ResourcePathSafety::isValidUtf8(teamInfoText))
	{
		GameLog::write("Title: team info is not valid UTF-8: %s\n", fileName.c_str());
		teamInfoText.clear();
		return false;
	}

	bool hasVisibleText = false;
	for (unsigned char character : teamInfoText)
	{
		if (character > 0x20)
		{
			hasVisibleText = true;
			break;
		}
	}
	if (!hasVisibleText)
	{
		GameLog::write("Title: team info is empty: %s\n", fileName.c_str());
		teamInfoText.clear();
		return false;
	}
	return true;
}
}

Title::Title(bool skipStartupVideos)
	: skipStartupVideos(skipStartupVideos)
{
	name = "Title";
	drawFullScreen = true;
	focusManager.setInputAwarePresentation();
	setPointerEventPreviewEnabled(true);
}

Title::~Title()
{
	freeResource();
}

void Title::playTitleBGM()
{
	std::string musicName = ResourceManager::instance().getActiveManifest().titleMusic;
	if (musicName.empty())
	{
		GameLog::write("Title: title music is empty\n");
		return;
	}

	const auto candidates = buildMediaAssetCandidates(
		MUSIC_FOLDER,
		musicName,
		{ ".mp3", ".ogg", ".wma", ".wav" });
	for (const std::string& candidate : candidates)
	{
		std::unique_ptr<char[]> data;
		int length = 0;
		if (!File::readFile(
				candidate,
				data,
				length,
				static_cast<int>(
					AudioDecodeSafety::MaxEncodedAudioBytes)) ||
			data == nullptr || length <= 0)
		{
			continue;
		}
		if (engine->loadBGM(data, length) == nullptr)
		{
			GameLog::write(
				"Title: can not decode title music %s\n",
				candidate.c_str());
			continue;
		}
		if (engine->playBGM() == nullptr)
		{
			GameLog::write(
				"Title: can not play title music %s\n",
				candidate.c_str());
			continue;
		}
		GameLog::write(
			"Title: playing title music %s\n",
			candidate.c_str());
		return;
	}

	GameLog::write(
		"Title: can not load or play title music %s\n",
		musicName.c_str());
}

void Title::init()
{
	freeResource();
	// 优先使用 Manifest Title.Menu；为空时回退到旧默认 ini\ui\title\title.menu.ini。
	const auto& manifest = ResourceManager::instance().getActiveManifest();
	std::string menuName = manifest.titleMenu;
	if (NewYearPeriod::containsCurrentLocalDate() && !manifest.titleNewYearMenu.empty())
	{
		menuName = manifest.titleNewYearMenu;
	}
	if (menuName.empty())
	{
		menuName = "ini\\ui\\title\\title.menu.ini";
	}
	loadMenuDefinition(menuName);
	// Legacy and third-party MOD title windows may override the base window
	// without carrying the newer aspect-fit fields. A title composition is
	// always authored in its declared base size, so keep the background and
	// controls on one uniform transform and soften the unused viewport bars.
	keepAspect = true;
	fadeMirroredBars = true;

	initBtn = getComponentByName<Button>("initBtn");
	exitBtn = getComponentByName<Button>("exitBtn");
	loadBtn = getComponentByName<Button>("loadBtn");
	teamBtn = getComponentByName<Button>("teamBtn");

	weather = std::make_shared<Weather>();
	weather->setPriority(epMax + 3);
	addChild(weather);
	loadingFadeMask = std::make_shared<FadeMask>();
	loadingFadeMask->name = "TitleLoadingFadeMask";
	loadingFadeMask->setFadeTime(LoadingFadeTime);
	loadingFadeMask->setPriority(epMax + 1);
	loadingFadeMask->visible = false;
	addChild(loadingFadeMask);

	setChildRectReferToParent();
	// SystemNotice already lays itself out in viewport coordinates. Attach it
	// after the title's aspect-fit transform so it is not scaled a second time.
	systemNotice = std::make_shared<SystemNotice>();
	systemNotice->setPriority(epMax);
	addChild(systemNotice);
	configureFocus();
}

void Title::freeResource()
{
	focusManager.clear();
	initBtn = nullptr;
	exitBtn = nullptr;
	loadBtn = nullptr;
	teamBtn = nullptr;
	weather = nullptr;
	loadingFadeMask = nullptr;
	systemNotice = nullptr;
	titleCompositionCanvas = nullptr;
	titleCompositionOriginalTarget = nullptr;
	titleCompositionCanvasWidth = 0;
	titleCompositionCanvasHeight = 0;
	drawingTitleComposition = false;
	pointerRipples.clear();

	result = erNone;
	ConfigDrivenPanel::freeResource();
}

void Title::configureFocus()
{
	focusManager.clear();
	const std::vector<std::string> focusOrder = focusManager.addLinearGroup(
		"title-actions",
		UIFocusLinearAxis::Vertical,
		{
			{ "new-game", initBtn, [this]() { startNewGame(); } },
			{ "load-game", loadBtn, [this]() { openSavedGame(); } },
			{ "team", teamBtn, [this]() { openTeamPage(); } },
			{ "exit", exitBtn, [this]() { exitApplication(); } }
		});
	focusManager.applyConfigDrivenFocusNavigation(
		*this,
		{
			{ "initBtn", "new-game" },
			{ "loadBtn", "load-game" },
			{ "teamBtn", "team" },
			{ "exitBtn", "exit" },
		});
	if (!focusOrder.empty())
	{
		focusManager.setDefaultFocus(focusOrder.front());
		focusManager.focusDefault();
	}
}

void Title::exitApplication()
{
	engine->stopBGM();
	result |= erExit;
	logicRunning = false;
}

void Title::openTeamPage()
{
	const auto& manifest = ResourceManager::instance().getActiveManifest();
	std::string teamInfoText;
	loadActiveTeamInfo(manifest.teamInfoFile, teamInfoText);

	std::string teamVideo = manifest.titleTeamVideo;
	if (!teamVideo.empty())
	{
		const std::string teamVideoPath = "video\\" + teamVideo;
		if (!File::activeResourceFileExist(teamVideoPath))
		{
			GameLog::write("Title: local team video is unavailable: %s\n",
				teamVideoPath.c_str());
			teamVideo.clear();
		}
	}
	if (teamVideo.empty() && teamInfoText.empty())
	{
		GameLog::write("Title: no local team content configured\n");
		return;
	}
	weather->fadeOut();
	engine->stopBGM();
	auto titleTeam = std::make_shared<TitleTeam>(teamVideo, teamInfoText);
	unsigned int returnValue = titleTeam->run();
	if ((returnValue & erExit) != 0)
	{
		result |= erExit;
		logicRunning = false;
		return;
	}
	tryCleanRes();
	playTitleBGM();
	weather->fadeInEx();
}

void Title::startNewGame()
{
	if (systemNotice != nullptr)
	{
		systemNotice->dismiss();
	}
	weather->fadeOut();
	engine->stopBGM();
	auto mainScene = std::make_shared<MainScene>(0);
	const unsigned int returnValue = mainScene->run();
	std::string failureMessage = mainScene->game != nullptr
		? mainScene->game->getLastLoadFailureMessage()
		: std::string();
	if (failureMessage.empty() && (returnValue & erInitError) != 0)
	{
		failureMessage = u8"游戏场景初始化失败";
	}
	if ((returnValue & erExit) != 0)
	{
		result |= erExit;
		logicRunning = false;
		return;
	}
	tryCleanRes();
	playTitleBGM();
	weather->fadeInEx();
	showSceneFailureNotice(
		failureMessage,
		0,
		true,
		(returnValue & erInitError) != 0);
}

void Title::openSavedGame()
{
	auto saveLoad = std::make_shared<SaveLoad>(false, true);
	saveLoad->setPriority(epMax + 2);
	addChild(saveLoad);
	unsigned int returnValue = saveLoad->run();
	const int selectedSaveIndex = saveLoad->index;
	if ((returnValue & erExit) != 0)
	{
		result |= erExit;
		logicRunning = false;
	}
	else if ((returnValue & erLoad) != 0)
	{
		if (systemNotice != nullptr)
		{
			systemNotice->dismiss();
		}
		if (loadingFadeMask != nullptr)
		{
			loadingFadeMask->visible = true;
			loadingFadeMask->fadeOut();
		}
		removeChild(saveLoad);
		saveLoad = nullptr;
		engine->stopBGM();
		auto mainScene = std::make_shared<MainScene>(selectedSaveIndex + 1);
		returnValue = mainScene->run();
		std::string failureMessage = mainScene->game != nullptr
			? mainScene->game->getLastLoadFailureMessage()
			: std::string();
		if (failureMessage.empty() && (returnValue & erInitError) != 0)
		{
			failureMessage = u8"游戏场景初始化失败";
		}
		if ((returnValue & erExit) != 0)
		{
			result |= erExit;
			logicRunning = false;
		}
		else
		{
			tryCleanRes();
			playTitleBGM();
			if (loadingFadeMask != nullptr)
			{
				loadingFadeMask->fadeIn();
				loadingFadeMask->visible = false;
			}
			showSceneFailureNotice(
				failureMessage,
				selectedSaveIndex + 1,
				false,
				(returnValue & erInitError) != 0);
		}
	}
	removeChild(saveLoad);
}

void Title::showSceneFailureNotice(
	const std::string& failureMessage,
	int saveIndex,
	bool newGame,
	bool initializationFailure)
{
	if (failureMessage.empty() || systemNotice == nullptr)
	{
		return;
	}
	std::string message;
	if (!initializationFailure)
	{
		message = u8"系统：游戏运行失败：";
	}
	else if (newGame)
	{
		message = u8"系统：新游戏初始化失败：";
	}
	else
	{
		message = std::string(u8"系统：读档失败（存档槽 ") +
			std::to_string(saveIndex) + u8"）：";
	}
	message += failureMessage;
	message += u8"。请保留日志以便进一步排查。";
	GameLog::write("Title: %s\n", message.c_str());
	systemNotice->showMessage(message, FailureNoticeTime);
}

void Title::onEvent()
{
	if (exitBtn != nullptr && exitBtn->getResult(erClick))
	{
		exitApplication();
		return;
	}
	if (teamBtn != nullptr && teamBtn->getResult(erClick))
	{
		openTeamPage();
	}
	if (initBtn != nullptr && initBtn->getResult(erClick))
	{
		startNewGame();
	}
	if (loadBtn != nullptr && loadBtn->getResult(erClick))
	{
		openSavedGame();
	}
}

bool Title::onInitial()
{
	init();
	weather->fadeIn();
	return true;
}

void Title::onExit()
{
}

void Title::onRun()
{
	// 按 Manifest Startup.Videos 顺序播放。
	if (skipStartupVideos)
	{
		GameLog::write("Title: startup videos skipped by launch argument\n");
	}
	else
	{
		const auto& videos = ResourceManager::instance().getActiveManifest().startupVideos;
		for (const auto& videoEntry : videos)
		{
			std::string videoName = videoEntry;
			if (videoName.empty())
			{
				continue;
			}
			if (!shouldPlayStartupVideo(videoName))
			{
				GameLog::write("Title: skip conditional startup video %s\n", videoEntry.c_str());
				continue;
			}
			std::string fullPath = "video\\" + videoName;
			auto vp = std::make_shared<VideoPage>(fullPath);
			unsigned int ret = vp->run();
			if ((ret & erExit) != 0)
			{
				result |= erExit;
				logicRunning = false;
				return;
			}
		}
	}
	playTitleBGM();
	focusManager.focusDefault();
}

void Title::onPreviewPointerEvent(AEvent& e)
{
	const bool primaryMouseDown = e.eventType == ET_MOUSEDOWN &&
		e.eventData == MBC_MOUSE_LEFT;
	if (!primaryMouseDown && e.eventType != ET_FINGERDOWN)
	{
		return;
	}
	if (rect.w <= 0 || rect.h <= 0 ||
		e.eventX < rect.x || e.eventY < rect.y ||
		e.eventX >= rect.x + rect.w || e.eventY >= rect.y + rect.h)
	{
		return;
	}

	AspectFitPointerRipple ripple;
	ripple.normalizedX = std::clamp(
		static_cast<float>(e.eventX - rect.x) / rect.w,
		0.0f,
		1.0f);
	ripple.normalizedY = std::clamp(
		static_cast<float>(e.eventY - rect.y) / rect.h,
		0.0f,
		1.0f);
	ripple.startTimeMilliseconds = getTime();
	removeExpiredPointerRipples();
	if (pointerRipples.size() >= MaximumPointerRippleCount)
	{
		return;
	}
	pointerRipples.push_back(ripple);
}

bool Title::onHandleEvent(AEvent & e)
{
	if (e.eventType == ET_KEYDOWN)
	{
#if !defined(__MOBILE__)
		if (e.eventData == KEY_F)
		{	
			if (engine->getKeyPress(KEY_LCTRL) || engine->getKeyPress(KEY_RCTRL))
			{
                auto fullScreenMode = engine->getWindowFullScreen();
                if (fullScreenMode == FullScreenMode::window) 
                {
                    fullScreenMode = FullScreenMode::windowFullScreen;
                }
                else if (fullScreenMode == FullScreenMode::windowFullScreen)
                {
                    fullScreenMode = FullScreenMode::fullScreen;
                }
                else
                {
                    fullScreenMode = FullScreenMode::window;
				}
				engine->setWindowFullScreen(fullScreenMode);
				Config::fullScreenMode = fullScreenMode;
				Config::save();
				return true;
			}
		}
#endif
		if (e.eventData == KEY_M)
		{
			if (engine->getKeyPress(KEY_LCTRL) || engine->getKeyPress(KEY_RCTRL))
			{
				engine->setMouseHardware(!engine->getMouseHardware());
				return true;
			}
		}

		if (dispatchKeyboardUIAction(e, *this))
		{
			return true;
		}
	}
	else if (e.eventType == ET_QUIT || e.eventType == ET_WINDOWCLOSE)
	{
		result |= erExit;
		logicRunning = false;
		return true;
	}
	return false;
}

bool Title::onHandleUIAction(UIAction action)
{
	return focusManager.handleAction(action);
}

bool Title::ensureTitleCompositionCanvas()
{
	if (engine == nullptr)
	{
		return false;
	}

	int width = 0;
	int height = 0;
	engine->getWindowSize(width, height);
	if (width <= 0 || height <= 0)
	{
		return false;
	}
	if (titleCompositionCanvas != nullptr &&
		titleCompositionCanvasWidth == width &&
		titleCompositionCanvasHeight == height)
	{
		return true;
	}

	titleCompositionCanvas = engine->createCanvasImage(width, height);
	if (titleCompositionCanvas == nullptr)
	{
		titleCompositionCanvasWidth = 0;
		titleCompositionCanvasHeight = 0;
		return false;
	}
	engine->setImageAlpha(titleCompositionCanvas, 255);
	titleCompositionCanvasWidth = width;
	titleCompositionCanvasHeight = height;
	return true;
}

void Title::removeExpiredPointerRipples()
{
	const UTime currentTime = getTime();
	pointerRipples.erase(
		std::remove_if(
			pointerRipples.begin(),
			pointerRipples.end(),
			[currentTime](const AspectFitPointerRipple& ripple)
			{
				return ripple.durationMilliseconds == 0 ||
					(currentTime >= ripple.startTimeMilliseconds &&
						currentTime - ripple.startTimeMilliseconds >=
							ripple.durationMilliseconds);
			}),
		pointerRipples.end());
}

bool Title::onBeginDrawComposition()
{
	if (!fadeMirroredBars || !ensureTitleCompositionCanvas())
	{
		return false;
	}

	titleCompositionOriginalTarget = engine->getRenderTarget();
	if (!engine->setSharedImageAsRenderTarget(
		titleCompositionCanvas))
	{
		titleCompositionOriginalTarget = nullptr;
		return false;
	}
	engine->renderClear(0, 0, 0, 0);
	drawingTitleComposition = true;
	return true;
}

bool Title::shouldDrawChildAfterComposition(const PElement& child) const
{
	return child == loadingFadeMask ||
		child == systemNotice ||
		std::dynamic_pointer_cast<SaveLoad>(child) != nullptr;
}

void Title::onEndDrawComposition(bool completed)
{
	if (!drawingTitleComposition || engine == nullptr)
	{
		return;
	}

	drawingTitleComposition = false;
	const _image originalTarget = titleCompositionOriginalTarget;
	titleCompositionOriginalTarget = nullptr;
	const bool restored =
		engine->restoreImageRenderTargetAfterAcceptedOperation(
			originalTarget,
			titleCompositionCanvas);
	if (!completed || !restored)
	{
		return;
	}

	const int contentWidth = baseWidth > 0 ? baseWidth : rect.w;
	const int contentHeight = baseHeight > 0 ? baseHeight : rect.h;
	Rect compositionSource = AspectFitLayout::calculateFittedRect(
		contentWidth,
		contentHeight,
		rect.w,
		rect.h);
	compositionSource.x += rect.x;
	compositionSource.y += rect.y;
	if (compositionSource.w <= 0 || compositionSource.h <= 0)
	{
		return;
	}
	removeExpiredPointerRipples();
	engine->drawAspectFitImage(
		titleCompositionCanvas,
		compositionSource,
		rect,
		true,
		255,
		getTime(),
		pointerRipples.empty() ? nullptr : &pointerRipples);
}

void Title::onDraw()
{
	if (!drawingTitleComposition)
	{
		ImageContainer::onDraw();
		return;
	}

	const bool drawMirroredBars = fadeMirroredBars;
	fadeMirroredBars = false;
	ImageContainer::onDraw();
	fadeMirroredBars = drawMirroredBars;
}

void Title::onDrawEnd()
{
	if (!visible || engine == nullptr
		|| !Element::isCurrentRunOwner(this)
		|| !focusManager.isFocusPresented())
	{
		return;
	}
	using GameInput::InputAction;
	const std::vector<ControllerPromptItem> items =
	{
		{ InputAction::NavigateUp, "选择" },
		{ InputAction::Confirm, "确认" }
	};
	ControllerPromptPresenter::drawBottomBar(
		engine, engine->inputActions(), items);
}
