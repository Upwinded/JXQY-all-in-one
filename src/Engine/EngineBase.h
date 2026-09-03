/*
SDL、FFmpeg 等底层都封装在这里。
*/

#pragma once

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 
#endif
#include <cmath>
#include <string>
#include <deque>
#include <string>
#include <vector>
#include <list>
#include <functional>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <random>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <unordered_map>

#ifndef SHF_USE_AUDIO
#define SHF_USE_AUDIO
#endif

#ifndef SHF_USE_VIDEO
#define SHF_USE_VIDEO
#endif

extern "C"
{

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>
//#ifdef __APPLE__
//#include "SDL3/SDL_metal.h"
//#endif
//#include "SDL3/SDL_main.h"

#ifdef SHF_USE_VIDEO
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#endif // SHF_USE_VIDEO

#include "minilzo.h"
}

#include "../File/File.h"
#include "../libconvert/libconvert.h"
#include "../Types/Types.h"
#include "../File/log.h"
#include "ImageTypes.h"
#include "MediaTypes.h"
#include "Timer.h"
#include "WindowTypes.h"

struct AspectFitPointerRipple;

namespace GameInput
{
enum class InputAction : std::uint8_t;
class PhysicalInputManager;
}

#ifdef _WIN32
#ifdef _MSC_VER
// 是否从资源文件读取LOGO
//#define USE_LOGO_RESOURCE
#ifdef USE_LOGO_RESOURCE
#include "windows.h"
#endif
#endif // _MSC_VER
#endif // _WIN32

#ifdef main
#undef main
#endif

class ConditionalLock
{
public:
    explicit ConditionalLock(std::mutex& mutex, bool shouldLock)
    {
        if (shouldLock)
        {
            _mutex = &mutex;
            _mutex->lock();
        }
        else
        {
            _mutex = nullptr;
        }
    }

    ~ConditionalLock()
    {
        if (_mutex != nullptr)
        {
            _mutex->unlock();
        }
    }

    ConditionalLock(const ConditionalLock&) = delete;
    ConditionalLock& operator=(const ConditionalLock&) = delete;

private:
    std::mutex* _mutex;
};

using Cursor_t = SDL_Cursor;
using _shared_cursor = std::shared_ptr<Cursor_t>;
#define make_shared_cursor(a) std::shared_ptr<Cursor_t>(a, [](Cursor_t* b){SDL_DestroyCursor(b);})

using Surface_t = SDL_Surface;
using _shared_surface = std::shared_ptr<Surface_t>;
#define make_shared_surface(a) std::shared_ptr<Surface_t>(a, [](Surface_t* b){SDL_DestroySurface(b);})

#define SaveBMPFormat SDL_PIXELFORMAT_ARGB8888
#define SaveBMPPixelBytes 4


struct CursorFrame
{
	int xOffset;
	int yOffset;
	_shared_cursor frame = _shared_cursor(nullptr);
	_shared_image softwareFrame = _shared_image(nullptr);
};

struct CursorImage
{
	int interval = 0;
	std::vector<CursorFrame> image;
	_shared_cursor nativeAnimatedCursor = nullptr;
};

#ifdef SHF_USE_AUDIO

struct AudioBuffer
{
	SDL_AudioSpec spec = {};
	MIX_Audio* audio = nullptr;
	std::vector<uint8_t> data;
	std::size_t decodedByteCount = 0;
	bool loop = false;
	bool positional = false;
	int durationMs = 0;
};

struct AudioChannel
{
	MIX_Track* track = nullptr;
	AudioBuffer* music = nullptr;
	bool loop = false;
	bool playing = false;
	bool paused = false;
	bool stopped = false;
	float volume = 1.0f;
	float positionX = 0.0f;
	float positionY = 0.0f;
};

typedef AudioChannel Channel_t;

#else
typedef int Channel_t;

#endif

class EngineBase;

struct SoundAutoRelease_t
{
	_channel c = nullptr;
	_music m = nullptr;
	bool stopped = false;
};

typedef std::function<void(uint8_t*, int)> AudioCallback;

struct TimeEx
{
	float beginTime;
	bool paused;
	float pauseBeginTime;
};

#ifdef SHF_USE_VIDEO

struct VideoSound
{
	float t = 0.0;
	bool stopped = false;
};

struct VideoImage
{
	_shared_image image = nullptr;
	float t = 0.0;
};

struct MediaContent
{
	int time;
	int64_t info;
	void* data;
};

struct MediaStream
{
	bool exists = false;
	AVFormatContext * formatCtx = nullptr;
	AVPacket * packet = nullptr;

	AVFrame * frame = nullptr;
	AVStream * stream = nullptr;
	AVCodecContext * codecCtx = nullptr;
	AVIOContext * customIoContext = nullptr;
	bool inputOpened = false;

	bool setTS = false;

    SDL_IOStream * rWops = nullptr;
    int64_t rWops_length = 0;

	float totalTime = 0;
	float timePerFrame = 0;
	float timeBasePacket = 0;
	int64_t timelineStartTimestamp = AV_NOPTS_VALUE;
	bool decodeEnd = false;
	int index = -1;
	bool stopped = false;
};

struct VideoStruct
{
	static constexpr int MaxAudioBufferBytes = 16 * 1024 * 1024;
	static constexpr int MaxAudioSampleRate = 384000;
	static constexpr int MaxAudioFrameSamples = MaxAudioSampleRate * 4;
	static constexpr std::uint64_t MaxDecodedVideoPixels = 64ULL * 1024ULL * 1024ULL;

	std::string fileName;

	std::vector<uint8_t> audioBuffer;

	MediaStream videoStream;
	MediaStream audioStream;

	bool running = false;

	float videoVolume = 1;
	SDL_AudioStream* audioOutputStream = nullptr;
	SDL_AudioSpec audioOutputSpec = {};
	float soundDelay = 0;
	float soundRate = 48.0;
	bool audioOutputFlushed = false;
	void * b = nullptr;
	
	int pixelFormat = 0;
	std::deque<VideoImage> videoImage;
	AVFrame * sFrame = nullptr;
	SwsContext * swsContext = nullptr;

	SDL_Rect rect;
	bool fullScreen = true;
	int loop = 0;
	
	bool pausedBeforePause = false;
	bool decodeEnd = false;
	uint64_t decodedVideoFrameCount = 0;
	uint64_t drainedVideoFrameCount = 0;
	float firstDecodedVideoTime = -1.0f;
	float lastDecodedVideoTime = -1.0f;
	float totalTime = 0;
	TimeEx time;
	float lastTime = 0;
	bool stopped = false;
};
//#define make_video(a) std::shared_ptr<Video_t>(a, [](Video_t* b){EngineBase::freeVideo(b); delete b;})

#else
#endif

typedef int (* AppEventHandler)(SDL_Event* e);

class EngineBase
{
	friend bool runMediaRuntimeTests();
	friend class GamepadWorldRuntimeTestAccess;
	friend class MobileExternalInputRuntimeTestAccess;
protected:
	EngineBase();
public:
	virtual ~EngineBase();

	const GameInput::PhysicalInputManager& inputActions() const;
	// Consumes only the current frame's pressed edge; held state remains readable.
	bool consumeInputAction(GameInput::InputAction inputAction);
	void releasePhysicalInputsForContextTransition();

	static std::mutex _mutex;
	static std::atomic<bool> multiThreadedMode;
	static std::atomic<uint32_t> ImageCount;

	_shared_image createImageFromPixelData(const uint8_t* pixelData, int width, int height);

	//初始化类函数
private:
	enum class PhysicalInputLifecycleRequest : std::uint8_t
	{
		None,
		Suspend,
		Resume
	};

	_shared_image logo = nullptr;
	unsigned int clLogoBG = 0x000000;
	unsigned int clBG = 0x000000;
	Uint8 myr = 0;

	SDL_Window * window = nullptr;
	std::unique_ptr<GameInput::PhysicalInputManager> physicalInputManager;
	bool gamepadSubsystemInitialized = false;
	std::atomic<PhysicalInputLifecycleRequest> physicalInputLifecycleRequest =
		PhysicalInputLifecycleRequest::None;

	InitErrorType initSDL(const std::string& windowCaption, int wWidth, int wHeight, FullScreenMode fullScreenMode, FullScreenSolutionMode fullScreenSolutionMode, int display);
	void destroySDL();
	void applyPhysicalInputLifecycleRequest();

	static bool enginebaseAppEventHandler(void* userdata, SDL_Event* event);
	_shared_image realScreen;

protected:

	std::recursive_mutex soundMutex;
	static std::atomic<SDL_Renderer*> renderer;
	static AppEventHandler _externalEventHandler;
	static std::atomic<bool> isBackGround;
	static constexpr std::uint64_t LifecycleBackgroundBit = 1ULL;
	static constexpr std::uint64_t LifecycleRenderClosedBit = 2ULL;
	static constexpr std::uint64_t LifecycleRevisionIncrement = 4ULL;
	std::atomic<std::uint64_t> applicationLifecycleRequestState = 0;
	std::uint64_t appliedApplicationLifecycleRevision = 0;
	std::atomic<bool> currentFrameReady = false;
	bool isFrameReady() const
	{
		return currentFrameReady.load();
	}

	int SetRenderTarget(SDL_Renderer* r, SDL_Texture* t);

	InitErrorType init(const std::string& windowCaption, int & wWidth, int & wHeight, FullScreenMode fullScreenMode, FullScreenSolutionMode fullScreenSolutionMode, int display, AppEventHandler eventHandler = NULL);
	void destroyEngineBase();
	int width = 0;
	int height = 0;
	int requestedLogicalWidth = 0;
	int requestedLogicalHeight = 0;
	std::atomic<std::uint32_t> logicalResizeGeneration = 0;
	std::atomic<std::uint32_t> acknowledgedLogicalResizeGeneration = 0;
	bool pendingLogicalScreenTextureResize = false;
	bool pendingWindowResize = false;
	void queueApplicationLifecycleRequest(Uint32 eventType);
	void applyApplicationLifecycleRequest();
	bool isRenderAdmissionClosed() const;
    FullScreenMode _fullScreenMode = FullScreenMode::window;
    FullScreenSolutionMode _fullScreenSolutionMode = FullScreenSolutionMode::original;
	void setFullScreen(FullScreenMode mode);
	void setWindowSize(int w, int h);
	std::vector<DesktopDisplayInfo> getDesktopDisplays() const;
	DesktopDisplaySettings getDesktopDisplaySettings() const;
	bool applyDesktopDisplaySettings(
		const DesktopDisplaySettings& settings);
	void getLogicalWindowSize(int& w, int& h) const;
	void calculateLogicalSizeForScreen(int screenWidth, int screenHeight, int& logicalWidth, int& logicalHeight) const;
	bool recreateLogicalScreenTexture();
	bool resizeLogicalScreen(int logicalWidth, int logicalHeight, bool resizeWindow);
	bool handleWindowSizeChanged(int screenWidth, int screenHeight);
	bool hardwareCursor = true;
	void getScreenInfo(int& w, int& h);

	//底层处理函数
private:
	Rect rect = {0, 0, 0, 0};
    Rect displayRect = {0, 0, 0, 0};
	void clearScreen();
	void displayScreen();
	void updateState();
	void updateRect(int tempWidth, int tempHeight, Rect & rect);

private:
	void * lzoMem = nullptr;
protected:
	void * getMem(int size);
	void freeMem(void * mem);
	int getLZOOutLen(int inLen);
	int lzoCompress(const void * src, unsigned int srcLen, void * dst, lzo_uint* dstLen);
	int lzoDecompress(const void * src, unsigned int srcLen, void * dst, lzo_uint* dstLen);

	//时间差需小于49天 -_-|||
private:
	static Timer timer;
	int FPS = 0;
	int FPSTime = 0;
	int FPSCounter = 0;
	void initTime();
	void countFPS();
protected:
	static UTime getTime();
	static int getRand(int max, int min = 0);
protected:
	void delay(unsigned int t);
	int getFPS();

	//鼠标样式的处理函数
private: 
	CursorImage cursorImage;
	_shared_cursor hiddenCursor = nullptr;
	int CursorImageIndex = -1;
	void clearCursor();
	void drawCursor();
	bool activateNativeAnimatedCursor();
	void calculateCursorReferencePosition(int inX, int inY, int* outX, int* outY);
protected:
	_shared_cursor loadCursorImageFromMem(std::unique_ptr<char[]>& data, int size, int x, int y);
	_shared_cursor createCursorImageFromSurface(
		Surface_t* surface, int x, int y);
	_shared_cursor createCursorImageFromPixelData(const uint8_t* pixelData,
		int width, int height, int x, int y);
	_shared_cursor createNativeAnimatedCursor(
		const std::vector<_shared_surface>& frameSurfaces,
		int interval,
		int x,
		int y);
	_shared_surface createCursorSurfaceFromPixelData(
		const uint8_t* pixelData,
		int width,
		int height);
	void setCursorImage(CursorImage * cursor);
	void showCursor();
	void hideCursor();
	bool getCursorVisible() const;
	bool softwareCursorHidden = false;

protected:
	//图片相关的函数

	struct CachedGrayscaleImage
	{
		std::weak_ptr<Image_t> source;
		_shared_image image = nullptr;
	};

	std::unordered_map<Image_t*, CachedGrayscaleImage> grayscaleImageCache;

	void drawImage(_shared_image image, SDL_Rect * src, SDL_Rect * dst);
	void drawImage(_shared_image image, SDL_Rect * rect);
	bool pointInImage(_shared_image image, int x, int y);
	_shared_image createNewImageFromImage(_shared_image image);
	_shared_image createGrayscaleImage(_shared_image image);
	_shared_image getGrayscaleImage(_shared_image image);
	void clearGrayscaleImageCache();
	_shared_image loadImageFromMem(std::unique_ptr<char[]>& data, int size);
	_shared_image loadImageFromFile(const std::string & fileName);
	int saveImageToFile(_shared_image image, int w, int h, const std::string & fileName);
	int saveImageToFile(_shared_image image, const std::string & fileName);
	int saveImageToMem(_shared_image image, int w, int h, std::unique_ptr<char[]>& data);
	int saveImageToMem(_shared_image image, std::unique_ptr<char[]>& data);
	int saveImageToPngMemory(_shared_image image, int width, int height,
		std::unique_ptr<char[]>& data);
	_shared_surface loadSurfaceFromMem(std::unique_ptr<char[]>& data, int size);
	_shared_image createImageFromSurface(Surface_t* surface);
	void drawImage(_shared_image image, int x, int y);
	void drawImage(_shared_image image, Rect * rect);
	void drawImage(_shared_image image, Rect * src, Rect * dst);
	void drawImageEx(_shared_image image, Rect* src, Rect* dst, float angle, Point* center);
	void drawAspectFitImage(
		_shared_image image,
		const Rect& sourceRect,
		const Rect& destinationRect,
		bool fadeMirroredBars,
		std::uint8_t alpha = 255,
		UTime mirroredBarsAnimationTime = 0,
		const std::vector<AspectFitPointerRipple>* pointerRipples = nullptr);
	//void freeImage(Image_t* image);
	void setImageAlpha(_shared_image image, unsigned char a);
	void setImageColorMode(_shared_image image, unsigned char r, unsigned char g, unsigned char b);
	void drawImageWithColor(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b);
	void drawImageWithBlendAlpha(_shared_image image, int x, int y, unsigned char alpha, SDL_BlendMode blendMode);
	bool getImageSize(_shared_image image, int& w, int& h);
	bool getImageContentBounds(_shared_image image, Rect& bounds, bool ignoreBlack);

	_shared_image createCanvasImage(int w = -1, int h = -1);
	bool setImageAsRenderTarget(_image image);
	bool setSharedImageAsRenderTarget(_shared_image image);
	bool restoreImageRenderTargetAfterAcceptedOperation(
		_image originalTarget,
		const _shared_image& activeTarget);
	_image getRenderTarget();
	void renderClear(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 0);
	void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
	void drawGeometry(_shared_image image, const std::vector<Vertex>& vertices, const std::vector<int>& indices);

private:
	enum class RenderTargetSessionKind
	{
		none,
		talk,
		saveScreen
	};

	struct RenderTargetSessionState
	{
		RenderTargetSessionKind kind = RenderTargetSessionKind::none;
		SDL_ThreadID ownerThread = 0;
		SDL_Renderer* sessionRenderer = nullptr;
		SDL_Texture* originalTarget = nullptr;
		_shared_image temporaryTarget = nullptr;
	};

	std::recursive_mutex renderTargetSessionMutex;
	RenderTargetSessionState renderTargetSession;
	struct RetainedRenderTarget
	{
		SDL_Renderer* renderer = nullptr;
		_shared_image texture = nullptr;
	};
	std::vector<RetainedRenderTarget>
		retainedRenderTargets;
	bool beginRenderTargetSession(
		RenderTargetSessionKind kind,
		int targetWidth,
		int targetHeight,
		const _shared_image& reusableTarget);
	_shared_image endRenderTargetSession(
		RenderTargetSessionKind kind);
	bool abortRenderTargetSession(
		RenderTargetSessionKind kind);
	void resetRenderTargetSessionForShutdown();
	bool restoreAcceptedRenderTarget(
		SDL_Renderer* activeRenderer,
		SDL_Texture* originalTarget,
		const _shared_image& temporaryTarget);
protected:
	//将对话字符串保存为Texture，用于提升绘图速度
	bool beginDrawTalk(int w, int h);
	bool beginDrawTalk(const _shared_image& target);
	_shared_image endDrawTalk();
	void drawTalk(const std::string& text, int x, int y, int size, unsigned int color);

	//从纹理像素数据生成BMP16格式截图
	_shared_image loadSaveShotFromPixels(int w, int h, const char* data, int size);
	//将渲染目标从屏幕变更为特殊Texture，用于保存截图
	bool beginSaveScreen();
	//将渲染目标从特殊Texture变回屏幕，用于保存截图
	_shared_image endSaveScreen();
	int saveImageToPixels(_shared_image image, int w, int h, std::unique_ptr<char[]>& s);

	_shared_image createRaindrop();
	_shared_image createSnowflake();

	void loadLogo();
	void fadeInLogo();
	void fadeOutLogo();

	//图像遮罩相关的函数
private:
	_shared_surface screenMask;
protected:
#define LUM_MASK_WIDTH 200
#define LUM_MASK_HEIGHT 100
#define LUM_MASK_MAX_ALPHA 35
#define LUM_MAX_LIGHTS 32
	_shared_image createMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a, bool safe = false);
	_shared_image createLumMask();
	//绘制遮罩
	void drawMask(_shared_image mask);
	void drawMask(_shared_image mask, Rect* dst);
	//图片与遮罩混合
	void drawImageWithMask(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMask(_shared_image image, int x, int y, _shared_image mask);
	void drawImageWithMask(_shared_image image, Rect *src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMask(_shared_image image, Rect *src, Rect * dst, _shared_image mask);
	//在图片上层覆盖遮罩
	void drawImageWithMaskEx(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMaskEx(_shared_image image, int x, int y, _shared_image mask);
	void drawImageWithMaskEx(_shared_image image, Rect *src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMaskEx(_shared_image image, Rect *src, Rect * dst, _shared_image mask);
	
	void setScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawScreenMask();

	//事件、鼠标位置等函数
private:
	int realMousePosX;
	int realMousePosY;
	EventList eventList;
	void handleEvent();
	void copyEvent(AEvent& s, AEvent& d);
	void clearEventList();
protected:
	void pumpEvents();
	std::uint32_t markLogicalResizePending();
	std::uint32_t recordLogicalResizeEvent();
	void finalizeLogicalResizeEventPump(
		bool resizeEventGenerated,
		std::uint32_t queuedResizeGeneration);
	bool hasPendingLogicalResizeEvent() const;
	void acknowledgeLogicalResizeEvent(
		std::uint32_t generation,
		int logicalWidth,
		int logicalHeight);
	int mouseX = -1;
	int mouseY = -1;
	int getEventCount();
	int getEvent(AEvent& event);
	void pushEvent(AEvent& event);
	bool getKeyPress(KeyCode key);
	bool getMousePress(MouseButtonCode button);
	void getMouse(int& x, int& y);
	std::vector<AEvent> getAllFingersPosition();

	void resetEvent();
	//设置是否使用SDL自带的鼠标样式显示，如果引擎自己画鼠标图标，鼠标位置更新会慢一些，但拖拽时同步性更好。
	void setCursorHardware(bool isHardware);

	//字符串显示相关的函数
private:
	std::string font = "";
	SDL_IOStream * fontData = nullptr;
	std::unique_ptr<char[]> fontBuffer = nullptr;
	std::unordered_map<int, TTF_Font*> fontCache;
	TTF_Font* getCachedFont(int size);
	void clearFontCache();
protected:
	void setFontFromMem(std::unique_ptr<char[]>& data, int size);
	_shared_image createText(const std::string& text, int size, unsigned int color, bool safe = false);
	_shared_image createTextWithFontData(
		const void* data,
		std::size_t dataSize,
		const std::string& text,
		int size,
		unsigned int color,
		int wrapWidth,
		bool safe = false);
	void drawText(const std::string& text,int x, int y, int size, unsigned int color);
	void setFontName(const std::string& fontName);
	

	// 音频函数
#ifdef SHF_USE_AUDIO
private:
	struct AudioChannelSlot
	{
		std::unique_ptr<Channel_t> channel;
		uint64_t generation = 1;
	};

	int initSoundSystem();
	void destroySoundSystem();
	void updateSoundSystem();
	_channel registerAudioChannel(std::unique_ptr<Channel_t> channel);
	Channel_t* resolveAudioChannel(_channel handle);
	void releaseAudioChannel(_channel handle);
	void releaseAudioChannelSlot(std::size_t slotIndex);
	void clearAudioChannels();

	// ponytail: fixed cache budget; add eviction only if profiling shows that
	// frequently reused sounds arrive after the cache has filled.
	static constexpr std::size_t ActionSoundCacheLimitBytes =
		16ULL * 1024ULL * 1024ULL;
	static std::vector<SoundAutoRelease_t> soundList;
	std::vector<AudioChannelSlot> channelSlots;
	std::unordered_map<std::string, _music> actionSoundCache;
	std::size_t actionSoundCacheBytes = 0;
	std::string actionSoundCacheScope;
#endif

protected:
	_music createMusic(const std::unique_ptr<char[]>& data, int size, bool loop, bool music3d, unsigned char priority = 128);
	void freeMusic(_music music);
#ifdef SHF_USE_AUDIO
	_music getCachedActionSound(const std::string& key);
	_music cacheActionSound(const std::string& key, _music music);
	void setActionSoundCacheScope(const std::string& scope);
	void clearActionSoundCache();
#endif
	//以中心位置播放音乐
	_channel playMusic(_music music, float volume);
	//指定坐标播放音乐
	_channel playMusic(_music music, float x, float y, float volume);
	void stopMusic(_channel channel);
	void stopSoundsExcept(_channel keepChannelA = nullptr, _channel keepChannelB = nullptr);
	void pauseMusic(_channel channel);
	bool pauseMusicIfPlaying(_channel channel);
	void resumeMusic(_channel channel);
	void setMusicPosition(_channel channel, float x, float y);
	void setMusicVolume(_channel channel, float volume);
	bool getMusicPlaying(_channel channel);
	bool soundAutoRelease(_music music, _channel channel);
	void checkSoundRelease();
	
private:
#ifdef SHF_USE_VIDEO
	//FFmpeg视频函数
	int initVideo();
	void destroyVideo();
	void freeMediaStream(MediaStream * mediaStream);
	
	//将所有创建的video在创建时自动加入videoList列表中
	//在声音回调函数触发时检查列表中所有的video，得到声音所属video中的下一帧audio数据
	static std::vector<_video> videoList;
	void clearVideoList();
	void addVideoToList(_video video);
	void deleteVideoFromList(_video video);
	void deleteVideoFromList(int index);
	
	int convert(AVCodecContext* codecCtx, AVFrame* frame, int out_sample_format,
		int out_sample_rate, int out_channels, std::vector<uint8_t>& outBuffer);

	int openVideoFile(_video video);
	static int read_packet(void *opaque, uint8_t *buf, int buf_size);
	static int64_t seek_packet(void *opaque, int64_t offset, int whence);
	void setMediaStream(MediaStream * mediaStream, std::string& fileName, AVMediaType mediaType);
	float initVideoTime(_video video);
	void setVideoTimePaused(_video video, bool paused);
	float setVideoTime(_video video, float time);
	float getVideoSoundRate(_video video);
	void enqueueDecodedAudioFrame(_video video);
	void enqueueDecodedVideoFrame(_video video);
	void decodeNextAudio(_video video);
	void decodeNextVideo(_video video);
	void checkVideoDecodeEnd(_video video);

protected:
	void pauseAllVideo();
	void resumeAllVideo();

private:
	void clearVideo(_video video);
	void rearrangeVideoFrame(_video video);

	SDL_PixelFormat getVideoPixelFormat(int originalFormat);
	
	void tryDecodeVideo(_video video);
#endif
public:
	void freeVideo(_video video);
	void updateAllVideoVolume(float volume);
protected:
	_video loadVideo(const std::string& fileName);
	void setVideoRect(_video video, Rect * rect);

	void runVideo(_video video);
	bool updateVideo(_video video);
	struct FullScreenVideoLayout
	{
		Rect source = { 0, 0, 0, 0 };
		Rect destination = { 0, 0, 0, 0 };
		bool needsBlackBackground = false;
	};
	static Rect calculateAspectFitVideoRect(int sourceWidth, int sourceHeight,
		int destinationWidth, int destinationHeight);
	static FullScreenVideoLayout calculateFullScreenVideoLayout(
		int sourceWidth,
		int sourceHeight,
		int destinationWidth,
		int destinationHeight);
	void drawVideoFrame(_video video);
	bool onVideoFrame(_video video);
	void pauseVideo(_video video);
	void resumeVideo(_video video);
	void stopVideo(_video video);
	void resetVideo(_video video);
	float getVideoTime(_video video);

	void setVideoLoop(_video video, int loop);
	bool getVideoStopped(_video video);
	
protected:	
	//每一帧开始和结尾处的处理，必须调用
	// Derived engines may add owner-thread lifecycle gates. The hook is checked
	// after the event pump and again before presentation so a lifecycle event
	// observed by that pump cannot be followed by renderer work.
	virtual bool canPrepareRenderFrame() const;
	void frameBegin();
	void frameEnd();

};
