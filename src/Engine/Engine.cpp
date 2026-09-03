#include "Engine.h"
#include "AudioDecodeSafety.h"
#include "LogicalResolutionPolicy.h"
#include "../Image/IMP.h"

namespace
{
std::string normalizeActionSoundCachePart(std::string value)
{
	convert::replaceAllString(value, "\\", "/");
	return value;
}

std::string getActionSoundCacheScope()
{
	return normalizeActionSoundCachePart(File::getAssetsCollectionRoot()) + "\n" +
		normalizeActionSoundCachePart(File::getActiveResourceRoot()) + "\n" +
		File::getActiveSaveNamespace();
}
}

Engine::Engine()
{
}

Engine::~Engine()
{
	destroyEngine();
}

int Engine::engineAppEventHandler(SDL_Event* event)
{
	Engine* engine = getInstance();
	switch (event->type)
	{
	case SDL_EVENT_QUIT:
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		// 用户发起的关闭请求仍需由当前场景决定如何响应。
		return 1;
	case SDL_EVENT_TERMINATING:
		engine->requestApplicationQuit();
		return 0;
	case SDL_EVENT_LOW_MEMORY:
		/* You will get this when your app is paused and iOS wants more memory.
			Release as much memory as possible.
		*/
		return 0;
	case SDL_EVENT_WILL_ENTER_BACKGROUND:
	case SDL_EVENT_DID_ENTER_BACKGROUND:
		engine->applicationBackgrounded.store(true);
		return 0;
	case SDL_EVENT_WILL_ENTER_FOREGROUND:
		return 0;
	case SDL_EVENT_DID_ENTER_FOREGROUND:
		engine->applicationBackgrounded.store(false);
		return 0;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		// Losing desktop window focus must not pause gameplay or media. True
		// application suspension is handled by the lifecycle background events.
		return 1;
	default:
		/* No special processing, add it to the event queue */
		return 1;
	}
}

void Engine::requestApplicationQuit()
{
	applicationQuitRequested.store(true);
}

void Engine::resetApplicationQuitRequest()
{
	applicationQuitRequested.store(false);
}

bool Engine::isApplicationQuitRequested() const
{
	return applicationQuitRequested.load();
}

bool Engine::isApplicationActive() const
{
	return !applicationBackgrounded.load() &&
		!isRenderAdmissionClosed();
}

bool Engine::isMainThread() const
{
	return SDL_IsMainThread();
}

const GameInput::PhysicalInputManager& Engine::inputActions() const
{
	return EngineBase::inputActions();
}

bool Engine::consumeInputAction(GameInput::InputAction inputAction)
{
	return EngineBase::consumeInputAction(inputAction);
}

void Engine::releasePhysicalInputsForContextTransition()
{
	EngineBase::releasePhysicalInputsForContextTransition();
}

bool Engine::canPrepareRenderFrame() const
{
	return EngineBase::canPrepareRenderFrame() &&
		isApplicationActive() &&
		!isApplicationQuitRequested();
}

void Engine::updateApplicationMediaState()
{
	const bool shouldPause = !isApplicationActive();
	if (shouldPause)
	{
		if (applicationMediaPaused.exchange(true))
		{
			return;
		}
		applicationPausedBGMChannel = pauseMusicIfPlaying(channelBGM)
			? channelBGM : nullptr;
		applicationPausedTalkChannel = pauseMusicIfPlaying(channelTalk)
			? channelTalk : nullptr;
#ifdef SHF_USE_VIDEO
		pauseAllVideo();
#endif
		return;
	}

	if (!applicationMediaPaused.exchange(false))
	{
		return;
	}
	resumeMusic(applicationPausedBGMChannel);
	resumeMusic(applicationPausedTalkChannel);
	applicationPausedBGMChannel = nullptr;
	applicationPausedTalkChannel = nullptr;
#ifdef SHF_USE_VIDEO
	resumeAllVideo();
#endif
}

Engine* Engine::getInstance()
{
	// Construct the process singleton on first use so its destructor runs
	// before the File and GameLog namespace-static state used by shutdown.
	static Engine engine;
	return &engine;
}

int Engine::init(std::string & windowCaption, int windowWidth, int windowHeight, FullScreenMode fullScreenMode, FullScreenSolutionMode fullScreenSolutionMode, int display)
{
	engineBaseActive = true;
	resetApplicationQuitRequest();
	applicationBackgrounded.store(false);
	applicationMediaPaused.store(false);
	applicationPausedBGMChannel = nullptr;
	applicationPausedTalkChannel = nullptr;
	width = LogicalResolutionPolicy::constrainWidth(windowWidth);
	height = LogicalResolutionPolicy::constrainHeight(windowHeight);

	ConditionalLock locker(_mutex, multiThreadedMode);
	if (EngineBase::init(windowCaption, width, height, fullScreenMode, fullScreenSolutionMode, display, engineAppEventHandler) != initOK)
	{
		return initError;
	}

	return initOK;
}

void Engine::destroyEngine()
{
	if (bgm != nullptr)
	{
		freeMusic(bgm);
		bgm = nullptr;
		channelBGM = nullptr;
		applicationPausedBGMChannel = nullptr;
	}
	if (talk != nullptr)
	{
		freeMusic(talk);
		talk = nullptr;
		channelTalk = nullptr;
		applicationPausedTalkChannel = nullptr;
	}
	if (engineBaseActive)
	{
		engineBaseActive = false;
		EngineBase::destroyEngineBase();
	}
}

void Engine::getWindowSize(int& w, int& h)
{
	getLogicalWindowSize(w, h);
}

void Engine::setWindowSize(int w, int h)
{
	LogicalResolutionPolicy::constrain(w, h);
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::setWindowSize(w, h);
}

void Engine::setWindowFullScreen(FullScreenMode mode)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::setFullScreen(mode);
}

std::vector<DesktopDisplayInfo> Engine::getDesktopDisplays()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getDesktopDisplays();
}

DesktopDisplaySettings Engine::getDesktopDisplaySettings()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getDesktopDisplaySettings();
}

bool Engine::applyDesktopDisplaySettings(
	const DesktopDisplaySettings& settings)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::applyDesktopDisplaySettings(settings);
}


void Engine::getScreenInfo(int& w, int& h)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::getScreenInfo(w, h);
}

FullScreenMode Engine::getWindowFullScreen()
{
    ConditionalLock locker(_mutex, multiThreadedMode);
    return EngineBase::_fullScreenMode;
}

_shared_image Engine::loadImageFromFile(const std::string & fileName)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::loadImageFromFile(fileName);
}

_shared_image Engine::loadImageFromMem(std::unique_ptr<char[]>& data, int size)
{
	if (data == nullptr || size <= 0)
	{
		return nullptr;
	}
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::loadImageFromMem(data, size);	
}

int Engine::saveImageToFile(_shared_image image, int w, int h, const std::string & fileName)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::saveImageToFile(image, w, h, fileName);
}

int Engine::saveImageToFile(_shared_image image, const std::string & fileName)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::saveImageToFile(image, fileName);
}

int Engine::saveImageToMem(_shared_image image, int w, int h, std::unique_ptr<char[]>& data)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::saveImageToMem(image, w, h, data);
}

int Engine::saveImageToMem(_shared_image image, std::unique_ptr<char[]>& data)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::saveImageToMem(image, data);
}

int Engine::saveImageToPngMemory(_shared_image image, int width, int height,
	std::unique_ptr<char[]>& data)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::saveImageToPngMemory(image, width, height, data);
}

bool Engine::pointInImage(_shared_image image, int x, int y)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::pointInImage(image, x, y);
}

_shared_image Engine::createNewImageFromImage(_shared_image image)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::createNewImageFromImage(image);
}

_shared_image Engine::getGrayscaleImage(_shared_image image)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getGrayscaleImage(image);
}

void Engine::setMouseFromImpImage(_shared_imp impImage)
{
	if (!SDL_IsMainThread())
	{
		GameLog::write(
			"Engine: mouse cursor images must be created on the SDL main thread\n");
		return;
	}
	if (impImage == nullptr)
	{
		ConditionalLock locker(_mutex, multiThreadedMode);
		EngineBase::setCursorImage(nullptr);
		return;
	}
	CursorImage mouse;
	mouse.interval = impImage->interval;
	mouse.image.resize(impImage->frame.size());
	std::vector<_shared_surface> cursorFrameSurfaces(
		mouse.image.size());

	for (size_t i = 0; i < mouse.image.size(); i++)
	{
		mouse.image[i].xOffset = 0;//impImage->frame[i].xOffset;
		mouse.image[i].yOffset = 0;// impImage->frame[i].yOffset;
		if (impImage->frame[i].dataLen > 0 && impImage->frame[i].data != nullptr)
		{
			ConditionalLock locker(_mutex, multiThreadedMode);
			cursorFrameSurfaces[i] = EngineBase::loadSurfaceFromMem(
				impImage->frame[i].data,
				impImage->frame[i].dataLen);
			mouse.image[i].frame =
				EngineBase::createCursorImageFromSurface(
					cursorFrameSurfaces[i].get(),
					mouse.image[i].xOffset,
					mouse.image[i].yOffset);
			mouse.image[i].softwareFrame =
				EngineBase::createImageFromSurface(
					cursorFrameSurfaces[i].get());
			impImage->frame[i].image = nullptr;
			impImage->frame[i].data = nullptr;
			impImage->frame[i].dataLen = 0;
			
		}
		else if (!impImage->frame[i].pixelData.empty() &&
			impImage->frame[i].pixelWidth > 0 && impImage->frame[i].pixelHeight > 0)
		{
			ConditionalLock locker(_mutex, multiThreadedMode);
			cursorFrameSurfaces[i] =
				EngineBase::createCursorSurfaceFromPixelData(
					impImage->frame[i].pixelData.data(),
					impImage->frame[i].pixelWidth,
					impImage->frame[i].pixelHeight);
			mouse.image[i].frame =
				EngineBase::createCursorImageFromSurface(
					cursorFrameSurfaces[i].get(),
					mouse.image[i].xOffset,
					mouse.image[i].yOffset);
			mouse.image[i].softwareFrame =
				EngineBase::createImageFromSurface(
					cursorFrameSurfaces[i].get());
			std::vector<uint8_t>().swap(impImage->frame[i].pixelData);
			impImage->frame[i].pixelWidth = 0;
			impImage->frame[i].pixelHeight = 0;
		}
		else
		{
			mouse.image[i].frame = nullptr;
			mouse.image[i].softwareFrame = nullptr;
		}
	}

	{
		ConditionalLock locker(_mutex, multiThreadedMode);
		mouse.nativeAnimatedCursor =
			EngineBase::createNativeAnimatedCursor(
				cursorFrameSurfaces,
				mouse.interval,
				mouse.image.empty()
					? 0
					: mouse.image.front().xOffset,
				mouse.image.empty()
					? 0
					: mouse.image.front().yOffset);
		EngineBase::setCursorImage(&mouse);
	}
	mouse.image.resize(0);
}

void Engine::setMouseHardware(bool hdCursor)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::setCursorHardware(hdCursor);
}

bool Engine::getMouseHardware()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::hardwareCursor;
}

void Engine::showCursor()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::showCursor();
}

void Engine::hideCursor()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::hideCursor();
}

bool Engine::getCursorVisible()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getCursorVisible();
}

int Engine::getEventCount()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getEventCount();
}

int Engine::getEvent(AEvent& event)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getEvent(event);
}

void Engine::pushEvent(AEvent& event)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::pushEvent(event);
}

void Engine::acknowledgeLogicalResizeEvent(
	std::uint32_t generation,
	int logicalWidth,
	int logicalHeight)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::acknowledgeLogicalResizeEvent(
		generation,
		logicalWidth,
		logicalHeight);
}

bool Engine::getKeyPress(KeyCode key)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getKeyPress(key);
}

bool Engine::getMousePressed(MouseButtonCode button)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getMousePress(button);
}

void Engine::getMousePosition(int& x, int& y)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::getMouse(x, y);
}

std::vector<AEvent> Engine::getAllFingersPosition()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getAllFingersPosition();
}

void Engine::pumpEvents()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	updateApplicationMediaState();
	EngineBase::pumpEvents();
	updateApplicationMediaState();
}

void Engine::frameBegin()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	updateApplicationMediaState();
	// Always let the SDL event pump run. Foreground lifecycle events are what
	// make a suspended application active again, so short-circuiting before
	// EngineBase::frameBegin() can leave the application permanently paused.
	EngineBase::frameBegin();
	updateApplicationMediaState();
	if (!isApplicationActive() || isApplicationQuitRequested())
	{
		currentFrameReady.store(false);
	}
}

void Engine::frameEnd()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	updateApplicationMediaState();
	if (!isApplicationActive() || isApplicationQuitRequested() ||
		!EngineBase::isFrameReady())
	{
		currentFrameReady.store(false);
		return;
	}
	drawFPSWithoutLock();
	if (!isApplicationActive() ||
		isApplicationQuitRequested() ||
		!EngineBase::isFrameReady())
	{
		currentFrameReady.store(false);
		return;
	}
	EngineBase::frameEnd();
}

void Engine::setMultiThreadedMode(bool enabled)
{
	multiThreadedMode.store(enabled);
}

bool Engine::isMultiThreadedMode() const
{
	return multiThreadedMode.load();
}

int Engine::getRand(int max, int min)
{
	return EngineBase::getRand(max, min);
}

UTime Engine::getTime()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getTime();
}

void Engine::delay(unsigned int t)
{
	EngineBase::delay(t);
}

int Engine::getFPS()
{
	return EngineBase::getFPS();
}

void Engine::drawFPS()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	drawFPSWithoutLock();
}

void Engine::drawFPSWithoutLock()
{
#if  defined(_WIN32)
//#define _DRAW_FPS
#endif // _CONSOLE_MODE

#ifdef _DRAW_FPS
	//std::string s = convert::formatString("%dfps,%0.3fs,%d,%d", getFPS(),((float)getTimeReferToParentTime())/ 1000, x, y);
	std::string s = convert::formatString("FPS:%d", getFPS());
	EngineBase::drawText(s, 0, 0, 25, 0xD0FFFFFF);
#endif // _DRAW_FPS
}

void Engine::drawImage(_shared_image image, int x, int y)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImage(image, x, y);
}

void Engine::drawImage(_shared_image image, Rect* src, Rect* dst)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImage(image, src, dst);
}

void Engine::drawImageEx(_shared_image image, Rect* src, Rect* dst, float angle, Point* center)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageEx(image, src, dst, angle / 3.14159265 * 180, center);
}

void Engine::drawAspectFitImage(
	_shared_image image,
	const Rect& sourceRect,
	const Rect& destinationRect,
	bool fadeMirroredBars,
	std::uint8_t alpha,
	UTime mirroredBarsAnimationTime,
	const std::vector<AspectFitPointerRipple>* pointerRipples)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawAspectFitImage(
		image,
		sourceRect,
		destinationRect,
		fadeMirroredBars,
		alpha,
		mirroredBarsAnimationTime,
		pointerRipples);
}

void Engine::drawImageWithAlpha(_shared_image image, int x, int y, unsigned char alpha)
{
	setImageAlpha(image, alpha);
	drawImage(image, x, y);
	setImageAlpha(image, 255);
}

void Engine::drawImageWithAlpha(_shared_image image, Rect * src, Rect * dst, unsigned char alpha)
{
	setImageAlpha(image, alpha);
	drawImage(image, src, dst);
	setImageAlpha(image, 255);
}

void Engine::drawImageWithColor(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageWithColor(image, x, y, r, g, b);
}

void Engine::drawImageWithColor(_shared_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b)
{
	setImageColorMode(image, r, g, b);
	drawImage(image, src, dst);
	setImageColorMode(image, 255, 255, 255);
}

void Engine::setImageColorMode(_shared_image image, unsigned char r, unsigned char g, unsigned char b)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::setImageColorMode(image, r, g, b);
}

//void Engine::freeImage(_image image)
//{
//	EngineBase::freeImage(image);
//}

bool Engine::getImageSize(_shared_image image, int &w, int &h)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getImageSize(image, w, h);
}

bool Engine::getImageContentBounds(_shared_image image, Rect& bounds, bool ignoreBlack)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getImageContentBounds(image, bounds, ignoreBlack);
}

_shared_image Engine::createCanvasImage(int w, int h)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::createCanvasImage(w, h);
}

bool Engine::setImageAsRenderTarget(_image image)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::setImageAsRenderTarget(image);
}

bool Engine::setSharedImageAsRenderTarget(_shared_image image)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::setSharedImageAsRenderTarget(image);
}

bool Engine::restoreImageRenderTargetAfterAcceptedOperation(
	_image originalTarget,
	const _shared_image& activeTarget)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::
		restoreImageRenderTargetAfterAcceptedOperation(
			originalTarget,
			activeTarget);
}

_image Engine::getRenderTarget()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getRenderTarget();
}

void Engine::renderClear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::renderClear(r, g, b, a);
}

void Engine::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::fillRect(x, y, w, h, r, g, b, a);
}

void Engine::drawGeometry(_shared_image image, const std::vector<Vertex>& vertices, const std::vector<int>& indices)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawGeometry(image, vertices, indices);
}


bool Engine::beginDrawTalk(int w, int h)
{
	return beginDrawTalkInternal(w, h, nullptr);
}

bool Engine::beginDrawTalk(const _shared_image& target)
{
	return target != nullptr &&
		beginDrawTalkInternal(0, 0, target);
}

bool Engine::beginDrawTalkInternal(
	int width,
	int height,
	const _shared_image& target)
{
	if (!SDL_IsMainThread())
	{
		GameLog::write(
			"Engine: talk drawing must run on the SDL main thread\n");
		return false;
	}
	{
		std::lock_guard<std::mutex> locker(_talk_drawing_state_mutex);
		if (_talk_drawing_state != TalkDrawingState::idle)
		{
			return false;
		}
		_talk_drawing_state = TalkDrawingState::beginning;
		_talk_drawing_thread = std::this_thread::get_id();
	}

	_talk_drawing_lock_held = multiThreadedMode.load();
	if (_talk_drawing_lock_held)
	{
		try
		{
			_mutex.lock();
		}
		catch (...)
		{
			_talk_drawing_lock_held = false;
			clearTalkDrawingState();
			throw;
		}
	}

	bool beganDrawing = false;
	try
	{
		beganDrawing = target != nullptr
			? EngineBase::beginDrawTalk(target)
			: EngineBase::beginDrawTalk(width, height);
	}
	catch (...)
	{
		releaseTalkDrawingLock();
		clearTalkDrawingState();
		throw;
	}

	if (!beganDrawing)
	{
		releaseTalkDrawingLock();
		clearTalkDrawingState();
		return false;
	}

	{
		std::lock_guard<std::mutex> locker(_talk_drawing_state_mutex);
		_talk_drawing_state = TalkDrawingState::drawing;
	}
	return true;
}

_shared_image Engine::endDrawTalk()
{
	{
		std::lock_guard<std::mutex> locker(_talk_drawing_state_mutex);
		if (_talk_drawing_state != TalkDrawingState::drawing ||
			_talk_drawing_thread != std::this_thread::get_id())
		{
			return nullptr;
		}
		_talk_drawing_state = TalkDrawingState::ending;
	}

	_shared_image result;
	try
	{
		result = EngineBase::endDrawTalk();
	}
	catch (...)
	{
		releaseTalkDrawingLock();
		clearTalkDrawingState();
		throw;
	}

	releaseTalkDrawingLock();
	clearTalkDrawingState();
	return result;
}

void Engine::clearTalkDrawingState()
{
	std::lock_guard<std::mutex> locker(_talk_drawing_state_mutex);
	_talk_drawing_state = TalkDrawingState::idle;
	_talk_drawing_thread = std::thread::id();
}

void Engine::releaseTalkDrawingLock()
{
	if (!_talk_drawing_lock_held)
	{
		return;
	}
	_talk_drawing_lock_held = false;
	_mutex.unlock();
}

void Engine::drawTalk(const std::string& text, int x, int y, int size, unsigned int color)
{
	{
		std::lock_guard<std::mutex> locker(_talk_drawing_state_mutex);
		if (_talk_drawing_state != TalkDrawingState::drawing ||
			_talk_drawing_thread != std::this_thread::get_id())
		{
			std::string error = "Must use beginDrawTalk() on this thread before drawTalk()";
			throw error;
		}
	}
	EngineBase::drawTalk(text, x, y, size, color);
}

_shared_image Engine::loadSaveShotFromPixels(int w, int h, const char* data, int size)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::loadSaveShotFromPixels(w, h, data, size);
}

_shared_image Engine::createImageFromPixelData(const uint8_t* pixelData, int width, int height)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::createImageFromPixelData(pixelData, width, height);
}

bool Engine::beginSaveScreen()
{
	return EngineBase::beginSaveScreen();
}

_shared_image Engine::endSaveScreen()
{
	return EngineBase::endSaveScreen();
}

int Engine::saveImageToPixels(_shared_image image, int w, int h, std::unique_ptr<char[]>& s)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::saveImageToPixels(image, w, h, s);
}

_shared_image Engine::createRaindrop()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::createRaindrop();
}

_shared_image Engine::createSnowflake()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::createSnowflake();
}

void Engine::setFontName(const std::string & fontName)
{
	if (!SDL_IsMainThread())
	{
		GameLog::write(
			"Engine: cached font source changes must run on the SDL main thread\n");
		return;
	}
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::setFontName(fontName);
}

void Engine::setFontFromMem(std::unique_ptr<char[]>& data, int size)
{
	if (!SDL_IsMainThread())
	{
		GameLog::write(
			"Engine: cached font source changes must run on the SDL main thread\n");
		return;
	}
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::setFontFromMem(data, size);
}

_shared_image Engine::createText(const std::string & text, int size, unsigned int color)
{
	if (!SDL_IsMainThread())
	{
		GameLog::write(
			"Engine: cached text rendering must run on the SDL main thread\n");
		return nullptr;
	}
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::createText(text, size, color, true);
}

_shared_image Engine::createTextWithFontData(
	const void* data,
	std::size_t dataSize,
	const std::string& text,
	int size,
	unsigned int color,
	int wrapWidth)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::createTextWithFontData(
		data, dataSize, text, size, color, wrapWidth, true);
}

void Engine::drawText(const std::string & text, int x, int y, int size, unsigned int color)
{
	if (!SDL_IsMainThread())
	{
		GameLog::write(
			"Engine: cached text rendering must run on the SDL main thread\n");
		return;
	}
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawText(text, x, y, size, color);
}

void Engine::setImageAlpha(_shared_image image, unsigned char a)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::setImageAlpha(image, a);
}

void Engine::drawImageWithBlendAlpha(_shared_image image, int x, int y, unsigned char alpha, SDL_BlendMode blendMode)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageWithBlendAlpha(image, x, y, alpha, blendMode);
}

void Engine::setScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::setScreenMask(r, g, b, a);
}

void Engine::drawScreenMask()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawScreenMask();
}

void Engine::drawScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	setScreenMask(r, g, b, a);
	drawScreenMask();
}

_shared_image Engine::createMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::createMask(r, g, b, a);
}

_shared_image Engine::createLumMask()
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::createLumMask();
}

//绘制遮罩
void Engine::drawMask(_shared_image mask)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawMask(mask);
}

void Engine::drawMask(_shared_image mask, Rect * dst)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawMask(mask, dst);
}

//绘制带遮罩的
void Engine::drawImageWithMask(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageWithMask(image, x, y, r, g, b, a);
}

void Engine::drawImageWithMask(_shared_image image, int x, int y, _shared_image mask)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageWithMask(image, x, y, mask);
}

void Engine::drawImageWithMask(_shared_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageWithMask(image, src, dst, r, g, b, a);
}

void Engine::drawImageWithMask(_shared_image image, Rect * src, Rect * dst, _shared_image mask)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageWithMask(image, src, dst, mask);
}

void Engine::drawImageWithMaskEx(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageWithMaskEx(image, x, y, r, g, b, a);
}

void Engine::drawImageWithMaskEx(_shared_image image, int x, int y, _shared_image mask)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageWithMaskEx(image, x, y, mask);
}

void Engine::drawImageWithMaskEx(_shared_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageWithMaskEx(image, src, dst, r, g, b, a);
}

void Engine::drawImageWithMaskEx(_shared_image image, Rect * src, Rect * dst, _shared_image mask)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::drawImageWithMaskEx(image, src, dst, mask);
}

void * Engine::getMem(int size)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	return EngineBase::getMem(size);
}

void Engine::freeMem(void * mem)
{
	ConditionalLock locker(_mutex, multiThreadedMode);
	EngineBase::freeMem(mem);
}

int Engine::getLZOOutLen(int inLen)
{
	return EngineBase::getLZOOutLen(inLen);
}

int Engine::lzoCompress(const void * src, unsigned int srcLen, void * dst, lzo_uint * dstLen)
{
	return EngineBase::lzoCompress(src, srcLen, dst, dstLen);
}

int Engine::lzoDecompress(const void * src, unsigned int srcLen, void * dst, lzo_uint * dstLen)
{
	return EngineBase::lzoDecompress(src, srcLen, dst, dstLen);
}

_music Engine::createMusic(const std::unique_ptr<char[]>& data, int size, bool loop, bool music3d, unsigned char priority)
{
	return EngineBase::createMusic(data, size, loop, music3d, priority);
}

_music Engine::createMusic(const std::string & fileName, bool loop, bool music3d, unsigned char priority)
{
	std::unique_ptr<char[]> data;
	int size = 0;
	if (!File::readFile(fileName, data, size,
		static_cast<int>(AudioDecodeSafety::MaxEncodedAudioBytes)))
	{
		GameLog::write("Music File %s Readed Error\n", fileName.c_str());
		return nullptr;
	}
	if (data == nullptr || size <= 0)
	{
		GameLog::write("Music File %s Readed Error\n", fileName.c_str());
		return nullptr;
	}
	_music result = createMusic(data, size, loop, music3d, priority);

	return result;
}

void Engine::freeMusic(_music music)
{
	EngineBase::freeMusic(music);
}

void Engine::setMusicPosition(_channel channel, float x, float y)
{
	EngineBase::setMusicPosition(channel, x, y);
}

void Engine::setMusicVolume(_channel channel, float volume)
{
	EngineBase::setMusicVolume(channel, volume);
}

_channel Engine::playMusic(_music music, float volume)
{
	return EngineBase::playMusic(music, volume);
}

_channel Engine::playMusic(_music music, float x, float y, float volume)
{
	return EngineBase::playMusic(music, x, y, volume);
}

void Engine::stopMusic(_channel channel)
{
	EngineBase::stopMusic(channel);
}

void Engine::pauseMusic(_channel channel)
{
	EngineBase::pauseMusic(channel);
}

void Engine::resumeMusic(_channel channel)
{
	EngineBase::resumeMusic(channel);
}

bool Engine::getMusicPlaying(_channel channel)
{
	return EngineBase::getMusicPlaying(channel);
}

bool Engine::soundAutoRelease(_music music, _channel channel)
{
	return EngineBase::soundAutoRelease(music, channel);
}

_music Engine::loadSound(const std::unique_ptr<char[]>& data, int size)
{
	return createMusic(data, size, false, true);
}

_music Engine::loadSound(const std::string & fileName)
{
	return createMusic(fileName, false, true);
}

_music Engine::loadCircleSound(const std::unique_ptr<char[]>& data, int size)
{
	return createMusic(data, size, true, true);
}

_music Engine::loadCircleSound(const std::string & fileName)
{
	return createMusic(fileName, true, true);
}

void Engine::setSoundVolume(float volume)
{
	soundVolume = volume;
}

float Engine::getSoundVolume()
{
	return soundVolume;
}

_channel Engine::playSound(_music music, float x, float y, float volume)
{
    return playMusic(music, x, y, volume);
}

_channel Engine::playSound(_music music, float x, float y)
{
	return playMusic(music, x, y, soundVolume);
}

_channel Engine::playSound(_music music)
{
	return playSound(music, 0, 0);
}

_channel Engine::playCachedSoundFile(const std::string& fileName,
	float x, float y, float volume)
{
	if (fileName.empty())
	{
		return nullptr;
	}
	const std::string scope = getActionSoundCacheScope();
	const std::string cacheKey = scope + "\n" +
		normalizeActionSoundCachePart(fileName);
	std::lock_guard<std::recursive_mutex> locker(soundMutex);
	setActionSoundCacheScope(scope);

	_music music = getCachedActionSound(cacheKey);
	bool cached = music != nullptr;
	if (music == nullptr)
	{
		music = loadSound(fileName);
		if (music == nullptr)
		{
			return nullptr;
		}
#if defined(JXQY_ENABLE_TEST_HOOKS)
		actionSoundDecodeCountForTests++;
#endif
		_music retainedMusic = cacheActionSound(cacheKey, music);
		if (retainedMusic != nullptr)
		{
			if (retainedMusic != music)
			{
				freeMusic(music);
				music = retainedMusic;
			}
			cached = true;
		}
	}

	if (volume < 0.0f)
	{
		volume = soundVolume;
	}
	_channel channel = EngineBase::playMusic(music, x, y, volume);
	if (channel == nullptr)
	{
		if (!cached)
		{
			freeMusic(music);
		}
		return nullptr;
	}
	if (cached)
	{
		return channel;
	}
	if (soundAutoRelease(music, channel))
	{
		return channel;
	}
	freeMusic(music);
	return nullptr;
}

void Engine::stopAllSounds()
{
	EngineBase::stopSoundsExcept(channelBGM, channelTalk);
}

_channel Engine::playSound(const std::unique_ptr<char[]>& data, int size, float x, float y, float volume)
{
	_music m = loadSound(data, size);
	if (m == nullptr)
	{
		return nullptr;
	}
	if (volume < 0)
	{
		volume = Engine::getInstance()->getSoundVolume();
	}
	_channel c = playSound(m, x, y, volume);
	if (c == nullptr)
	{
		freeMusic(m);
		return nullptr;
	}
	if (soundAutoRelease(m, c))
	{
		return c;
	}
	else
	{
		freeMusic(m);
		return nullptr;
	}
}

_music Engine::loadBGM(const std::unique_ptr<char[]>& data, int size)
{
	if (bgm != nullptr)
	{
		freeMusic(bgm);
		bgm = nullptr;
		channelBGM = nullptr;
		applicationPausedBGMChannel = nullptr;
	}
	bgm = createMusic(data, size, true, false, 0);
	return bgm;
}

_music Engine::loadBGM(const std::string & fileName)
{
	if (bgm != nullptr)
	{
		freeMusic(bgm);
		bgm = nullptr;
		channelBGM = nullptr;
		applicationPausedBGMChannel = nullptr;
	}
	bgm = createMusic(fileName, true, false, 0);
	return bgm;
}

void Engine::setBGMVolume(float volume)
{
	bgmVolume = volume;
	EngineBase::setMusicVolume(channelBGM, bgmVolume);
	updateAllVideoVolume(bgmVolume);
}

float Engine::getBGMVolume()
{
	return bgmVolume;
}

_channel Engine::playBGM()
{
	channelBGM = playMusic(bgm, bgmVolume);
	return channelBGM;
}

void Engine::stopBGM()
{
	EngineBase::stopMusic(channelBGM);
	channelBGM = nullptr;
	applicationPausedBGMChannel = nullptr;
}

void Engine::pauseBGM()
{
	EngineBase::pauseMusic(channelBGM);
}

void Engine::resumeBGM()
{
	EngineBase::resumeMusic(channelBGM);
}

_music Engine::loadTalk(const std::unique_ptr<char[]>& data, int size)
{
	if (talk != nullptr)
	{
		freeMusic(talk);
		talk = nullptr;
		channelTalk = nullptr;
		applicationPausedTalkChannel = nullptr;
	}
	talk = createMusic(data, size, false, false, 0);
	return talk;
}

_music Engine::loadTalk(const std::string & fileName)
{
	if (talk != nullptr)
	{
		freeMusic(talk);
		talk = nullptr;
		channelTalk = nullptr;
		applicationPausedTalkChannel = nullptr;
	}
	talk = createMusic(fileName, false, false, 0);
	return talk;
}

void Engine::setTalkVolume(float volume)
{
	talkVolume = volume;
	EngineBase::setMusicVolume(channelTalk, volume);
}

float Engine::getTalkVolume()
{
	return talkVolume;
}

_channel Engine::playtalk()
{
	channelTalk = playMusic(talk, talkVolume);
	return channelTalk;
}

void Engine::stopTalk()
{
	EngineBase::stopMusic(channelTalk);
	channelTalk = nullptr;
	applicationPausedTalkChannel = nullptr;
}

void Engine::pauseTalk()
{
	EngineBase::pauseMusic(channelTalk);
}

void Engine::resumeTalk()
{
	EngineBase::resumeMusic(channelTalk);
}

_video Engine::loadVideo(const std::string & fileName)
{
	auto v = EngineBase::loadVideo(fileName);
	if (v != nullptr)
	{
		v->videoVolume = bgmVolume;
	}
	return v;
}

void Engine::setVideoRect(_video v, Rect * rect)
{
	EngineBase::setVideoRect(v, rect);
}

void Engine::freeVideo(_video v)
{
	EngineBase::freeVideo(v);
}

void Engine::runVideo(_video v)
{
	EngineBase::runVideo(v);
}

bool Engine::updateVideo(_video v)
{
	return EngineBase::updateVideo(v);
}

void Engine::drawVideoFrame(_video v)
{
	EngineBase::drawVideoFrame(v);
}

bool Engine::onVideoFrame(_video v)
{
	return EngineBase::onVideoFrame(v);
}

void Engine::pauseVideo(_video v)
{
	EngineBase::pauseVideo(v);
}

void Engine::resumeVideo(_video v)
{
	EngineBase::resumeVideo(v);
}

void Engine::stopVideo(_video v)
{
	EngineBase::stopVideo(v);
}

void Engine::resetVideo(_video v)
{
	EngineBase::resetVideo(v);
}

void Engine::setVideoLoop(_video v, int loop)
{
	EngineBase::setVideoLoop(v, loop);
}

bool Engine::getVideoStopped(_video v)
{
	return EngineBase::getVideoStopped(v);
}

float Engine::getVideoTime(_video v)
{
	return EngineBase::getVideoTime(v);
}
