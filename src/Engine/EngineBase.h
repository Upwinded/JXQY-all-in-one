/*
SDL、FMOD、FFMPEG等底层都封装在这里。
*/

#pragma once

#include <string>
#include <deque>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <mutex>
#include <atomic>


#ifndef SHF_USE_AUDIO
#define SHF_USE_AUDIO
#endif

#ifndef SHF_USE_VIDEO
#define SHF_USE_VIDEO
#endif

extern "C"
{

#ifdef SHF_USE_AUDIO
#include "fmod.h"
#endif // SHF_USE_AUDIO
#ifdef __APPLE__
#include "SDL2/SDL.h"
#include "SDL2_image/SDL_image.h"
#include "SDL2_ttf/SDL_ttf.h"
#include "SDL2/SDL_main.h"
#else
#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"
#include "SDL_main.h"
#endif

#ifdef SHF_USE_VIDEO
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
#endif // SHF_USE_VIDEO

#include "../../3rd/minilzo/minilzo.h"
}

#include "../File/File.h"
#include "../libconvert/libconvert.h"
#include "../Types/Types.h"
#include "../File/log.h"

#if defined(__IPHONEOS__)
#ifndef __MOBILE__
#define __MOBILE__
#endif // !__MOBILE__
#endif // defined(__IPHONEOS__)

#ifdef __ANDROID__
#ifndef __MOBILE__
#define __MOBILE__
#endif // !__MOBILE__
#endif // __ANDROID__

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

enum class FullScreenMode
{
    window = 0,
    windowFullScreen = 1,
    fullScreen = 2
};

enum class FullScreenSolutionMode
{
    original = 0,
    adjust = 1,
    forceToUseSetting = 2
};

struct Rect
{
	int x;
	int y;
	int w;
	int h;

	bool PointInRect(int px, int py)
	{
		return px >= x && px < x + w && py >= y && py < y + h;
	}
};

typedef Rect Rect_t;
typedef std::shared_ptr<Rect_t> _rect;

typedef SDL_Cursor Cursor_t;
typedef std::shared_ptr<Cursor_t> _shared_cursor;
#define make_shared_cursor(a) std::shared_ptr<Cursor_t>(a, [](Cursor_t* b){SDL_FreeCursor(b);})

typedef SDL_Texture Image_t;
typedef std::shared_ptr<Image_t> _shared_image;
#define make_shared_image(a) std::shared_ptr<Image_t>(a, [](Image_t* b){SDL_DestroyTexture(b);})

#define make_safe_shared_image(a) std::shared_ptr<Image_t>(a, [](Image_t* b){std::lock_guard<std::mutex> locker(EngineBase::_mutex); SDL_DestroyTexture(b);})

typedef SDL_Surface Surface_t;
typedef std::shared_ptr<Surface_t> _shared_surface;
#define make_shared_surface(a) std::shared_ptr<Surface_t>(a, [](Surface_t* b){SDL_FreeSurface(b);})

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
};

#ifdef SHF_USE_AUDIO

typedef FMOD_SOUND Music_t;
typedef FMOD_CHANNEL Channel_t;

//#define make_music(a) std::shared_ptr<Music_t>(a, [](Music_t* b){FMOD_Sound_Release(b);})
//#define make_channel(a) std::shared_ptr<Channel_t>(a, [](Channel_t* b){return;})

#else
typedef int Music_t;
typedef int Channel_t;

#endif

//typedef std::shared_ptr<Music_t> _music;
//typedef std::shared_ptr<Channel_t> _channel;


typedef Music_t* _music;
typedef Channel_t* _channel;

struct SoundAutoRelease_t
{
	_channel c = nullptr;
	_music m = nullptr;
	bool stopped = false;
};

struct SoundAutoReleaseList
{
	std::vector<SoundAutoRelease_t> sound;
};

typedef std::function<void(uint8_t*, int)> AudioCallback;

struct TimeEx
{
	double beginTime;
	bool paused;
	double pauseBeginTime;
};

#ifdef SHF_USE_VIDEO

struct VideoSound
{
	_channel c = nullptr;
	_music m = nullptr;
	double t = 0.0;
	bool stopped = false;
};

struct VideoImage
{
	_shared_image image = nullptr;
	double t = 0.0;
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

	bool setTS = false;

    SDL_RWops * rWops = nullptr;
    int rWops_length = 0;

	double totalTime = 0;
	double timePerFrame = 0;
	double timeBasePacket = 0;
	double startTime = 0;
	bool decodeEnd = false;
	int index = -1;
	bool stopped = false;
};

struct VideoStruct
{
	std::string fileName;

	char * buffer = nullptr;

	MediaStream videoStream;
	MediaStream audioStream;

	bool running = false;
	FMOD_CHANNELGROUP * cg = nullptr;

	float videoVolume = 1;
	std::deque<VideoSound> videoSounds;
	double soundDelay = 0;
	double soundRate = 48.0;
	FMOD_SYSTEM * soundSystem = nullptr;
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
	double totalTime = 0;
	TimeEx time;
	double lastTime = 0;
	bool stopped = false;
};
typedef VideoStruct Video_t;
//#define make_video(a) std::shared_ptr<Video_t>(a, [](Video_t* b){EngineBase::freeVideo(b); delete b;})

#else
typedef int Video_t;
#endif

//typedef std::shared_ptr<Video_t> _video;
typedef Video_t* _video;

class Timer
{
public:
	Timer();
	Timer(Timer* parent);
	virtual ~Timer();
	void setParent(Timer* parent);
	UTime get();
	void set(UTime t);
	bool getPaused();
	void setPaused(bool paused);
	void reInit();

private:
	void addChild(Timer* time);
	void removeChild(Timer* time);
	UTime getAbsolute();

private:
	UTime _beginTime = 0;
	bool _paused = false;
	UTime _pauseBeginTime = 0;
	Timer* _parent = nullptr;
	std::vector<Timer*> _children;
};

typedef int (* AppEventHandler)(SDL_Event* e);

class EngineBase
{
protected:
	EngineBase();
public:
	virtual ~EngineBase();

	static std::mutex _mutex;
	//初始化类函数
private:
	_shared_image logo = nullptr;
	unsigned int clLogoBG = 0x000000;
	unsigned int clBG = 0x000000;
	Uint8 myr = 0;

	SDL_Window * window = nullptr;
	
	int windowWidth;
	int windowHeight;
	InitErrorType initSDL(const std::string& windowCaption, int wWidth, int wHeight, FullScreenMode fullScreenMode, FullScreenSolutionMode fullScreenSolutionMode);
	void destroySDL();

	static int enginebaseAppEventHandler(void* userdata, SDL_Event* event);
	_shared_image realScreen;

protected:


    std::mutex soundMutex;
	static std::atomic<SDL_Renderer*> renderer;
	static std::atomic<SDL_Texture*> tempRenderTarget;
	static AppEventHandler _externalEventHandler;
	static std::atomic<bool> isBackGround;

	int SetRenderTarget(SDL_Renderer* r, SDL_Texture* t);

	InitErrorType init(const std::string& windowCaption, int & wWidth, int & wHeight, FullScreenMode fullScreenMode, FullScreenSolutionMode fullScreenSolutionMode, AppEventHandler eventHandler = NULL);
	void destroyEngineBase();
	int width = 0;
	int height = 0;
    FullScreenMode _fullScreenMode = FullScreenMode::window;
    FullScreenSolutionMode _fullScreenSolutionMode = FullScreenSolutionMode::original;
	void setFullScreen(FullScreenMode mode);
	void setWindowSize(int w, int h);
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
protected:
	void delay(unsigned int t);
	int getFPS();

	//鼠标样式的处理函数
private: 
	CursorImage cursorImage;
	int CursorImageIndex = -1;
	void clearCursor();
	void drawCursor();
    void updateCursor();
	void calculateCursor(int inX, int inY, int* outX, int* outY);
	void destroyCursor();
protected:
	_shared_cursor loadCursorFromMem(std::unique_ptr<char[]>& data, int size, int x, int y);
	void setCursor(CursorImage * cursor);
	void showCursor();
	void hideCursor();
	bool softwareCursorHidden = false;

protected:
//图片相关的函数

	void drawImage(_shared_image image, SDL_Rect * src, SDL_Rect * dst);
	void drawImage(_shared_image image, SDL_Rect * rect);
	bool pointInImage(_shared_image image, int x, int y);
	_shared_image createNewImageFromImage(_shared_image image);
	_shared_image loadImageFromMem(std::unique_ptr<char[]>& data, int size);
	_shared_image loadImageFromFile(const std::string & fileName);
	int saveImageToFile(_shared_image image, int w, int h, const std::string & fileName);
	int saveImageToFile(_shared_image image, const std::string & fileName);
	int saveImageToMem(_shared_image image, int w, int h, std::unique_ptr<char[]>& data);
	int saveImageToMem(_shared_image image, std::unique_ptr<char[]>& data);
	_shared_surface loadSurfaceFromMem(std::unique_ptr<char[]>& data, int size);
	void drawImage(_shared_image image, int x, int y);
	void drawImage(_shared_image image, Rect * rect);
	void drawImage(_shared_image image, Rect * src, Rect * dst);
	void drawImageEx(_shared_image image, Rect* src, Rect* dst, double angle, Point* center);
	//void freeImage(Image_t* image);
	void setImageAlpha(_shared_image image, unsigned char a);
	void setImageColorMode(_shared_image image, unsigned char r, unsigned char g, unsigned char b);
	void drawImageWithColor(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b);
	int getImageSize(_shared_image image, int& w, int& h);

private:
	//used by save screen
	Image_t* originalTex = nullptr;
protected:
	//将对话字符串保存为Texture，用于提升绘图速度
	bool beginDrawTalk(int w, int h);
	_shared_image endDrawTalk();
	void drawTalk(const std::string& text, int x, int y, int size, unsigned int color);
	void drawSolidUnicodeText(const std::wstring& text, int x, int y, int size, unsigned int color);

	//从纹理像素数据生成BMP16格式截图
	_shared_image loadSaveShotFromPixels(int w, int h, std::unique_ptr<char[]>& data, int size);
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
#define LUM_MASK_WIDTH 800
#define LUM_MASK_HEIGHT 400
#define LUM_MASK_MAX_ALPHA 80
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
	int mouseX = -1;
	int mouseY = -1;
	int getEventCount();
	int getEvent(AEvent& event);
	void pushEvent(AEvent& event);
	bool getKeyPress(KeyCode key);
	bool getMousePress(MouseButtonCode button);
	void getMouse(int& x, int& y);
	void resetEvent();
	//设置是否使用SDL自带的鼠标样式显示，如果引擎自己画鼠标图标，鼠标位置更新会慢一些，但拖拽时同步性更好。
	void setCursorHardware(bool isHardware);

	//字符串显示相关的函数
private:
	std::string font = "";
	SDL_RWops * fontData = nullptr;
	std::unique_ptr<char[]> fontBuffer = nullptr;
protected:
	void setFontFromMem(std::unique_ptr<char[]>& data, int size);
	_shared_image createUnicodeText(const std::wstring& text, int size, unsigned int color);
	void drawUnicodeText(const std::wstring& text, int x, int y, int size, unsigned int color);
	_shared_image createText(const std::string& text, int size, unsigned int color, bool safe = false);
	void drawText(const std::string& text,int x, int y, int size, unsigned int color);
	void setFontName(const std::string& fontName);
	

	//FMOD音频函数
#ifdef SHF_USE_AUDIO
private:
	int initSoundSystem();
	void destroySoundSystem();
	void updateSoundSystem();


	FMOD_SYSTEM * soundSystem;

	int maxChannel = 512;

	static SoundAutoReleaseList soundList;
	static FMOD_RESULT F_CALLBACK autoReleaseSound(FMOD_CHANNELCONTROL *chanControl, FMOD_CHANNELCONTROL_TYPE controlType, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType, void *commandData1, void *commandData2);
#endif

protected:
	_music createMusic(const std::unique_ptr<char[]>& data, int size, bool loop, bool music3d, unsigned char priority = 128);
	static void freeMusic(_music music);
	//以中心位置播放音乐
	_channel playMusic(_music music, float volume);
	//指定坐标播放音乐
	_channel playMusic(_music music, float x, float y, float volume);
	void stopMusic(_channel channel);
	void pauseMusic(_channel channel);
	void resumeMusic(_channel channel);
	void setMusicPosition(_channel channel, float x, float y);
	void setMusicVolume(_channel channel, float volume);
	bool getMusicPlaying(_channel channel);
	bool soundAutoRelease(_music music, _channel channel);
	void checkSoundRelease();
	
private:
#ifdef SHF_USE_VIDEO
	//FFMPEG视频函数
	static const int videoConvertSize = 192000;
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
	
	static FMOD_RESULT F_CALLBACK audioCallback(FMOD_CHANNELCONTROL *chanControl, FMOD_CHANNELCONTROL_TYPE controlType, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType, void *commandData1, void *commandData2);
	int convert(AVCodecContext* codecCtx, AVFrame* frame, int out_sample_format, int out_sample_rate, int out_channels, uint8_t* out_buf);

	int openVideoFile(_video video);
	static int read_packet(void *opaque, uint8_t *buf, int buf_size);
	void setMediaStream(MediaStream * mediaStream, std::string& fileName, AVMediaType mediaType);
	double initVideoTime(_video video);
	void setVideoTimePaused(_video video, bool paused);
	double setVideoTime(_video video, double time);
	double getVideoSoundRate(_video video);
	void decodeNextAudio(_video video);
	void decodeNextVideo(_video video);
	void checkVideoDecodeEnd(_video video);
	void pauseAllVideo();
	void resumeAllVideo();
	void clearVideo(_video video);
	void rearrangeVideoFrame(_video video);

	int getVideoPixelFormat(int originalFormat);
	
	void tryDecodeVideo(_video video);
#ifdef SHF_USE_AUDIO
	_music createVideoRAW(FMOD_SYSTEM * system, char * data, int size, bool loop, bool music3d, FMOD_SOUND_FORMAT format, int numchannels, int defaultfrequency, unsigned char priority = 128);
#endif
#endif
public:
	void freeVideo(_video video);
protected:
	_video loadVideo(const std::string& fileName);
	void setVideoRect(_video video, Rect * rect);

	void runVideo(_video video);
	bool updateVideo(_video video);
	void drawVideoFrame(_video video);
	bool onVideoFrame(_video video);
	void pauseVideo(_video video);
	void resumeVideo(_video video);
	void stopVideo(_video video);
	void resetVideo(_video video);
	double getVideoTime(_video video);

	void setVideoLoop(_video video, int loop);
	bool getVideoStopped(_video video);
	
protected:	
	//每一帧开始和结尾处的处理，必须调用
	void frameBegin();
	void frameEnd();

};
