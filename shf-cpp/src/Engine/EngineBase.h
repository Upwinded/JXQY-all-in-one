/*
SDL、FMOD、FFMPEG等底层都封装在这里。
*/

#pragma once

#include <string>

#include "../File/File.h"
#include "../libconvert/libconvert.h"
#include "../Types/Types.h"

#include "FMOD/fmod.h"

extern "C"
{
#include "SDL2/sdl.h"
#include "SDL2/sdl_image.h"
#include "SDL2/sdl_ttf.h"

#include "minilzo/minilzo.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}

#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <mutex>

#ifdef _WIN32
#ifdef _MSC_VER
#include "windows.h"
#define SDL_MAIN_HANDLED
#define USE_LOGO_RESOURCE
#endif // _MSC_VER
#endif // _WIN32

#ifdef main
#undef main
#endif

#define BMP16Format SDL_PIXELFORMAT_RGB565

struct SoundAutoRelease
{
	_channel c = NULL;
	_music m = NULL;
};

struct SoundAutoReleaseList
{
	std::vector<SoundAutoRelease> sound = {};
};

struct TimeEx
{
	double beginTime;
	bool paused;
	double pauseBeginTime;
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

	AVFormatContext * formatCtx = NULL;
	AVPacket * packet = NULL;

	AVFrame * frame = NULL;
	AVStream * stream = NULL;
	AVCodecContext * codecCtx = NULL;
	AVCodec * codec = NULL;

	bool setTS = false;

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

	char * buffer = NULL;

	MediaStream videoStream;
	MediaStream audioStream;

	bool running = false;
	FMOD_CHANNELGROUP * cg = NULL;

	float videoVolume = 1;
	std::vector<_channel> videoSounds = {};
	std::vector<_channel> videoSoundChannels = {};
	std::vector<double> soundTime;
	double soundDelay = 0;
	double soundRate = 48.0;
	FMOD_SYSTEM * soundSystem = NULL;
	void * b = NULL;

	std::vector<_image> image = {};
	AVFrame * sFrame = NULL;
	SwsContext * swsContext = NULL;

	std::vector<double> videoTime;
	SDL_Rect rect;
	bool fullScreen = true;
	int loop = 0;

	bool pausedBeforePause = false;
	bool decodeEnd = false;
	double totalTime = 0;
	TimeEx time;
	bool stopped = false;
};

typedef VideoStruct* video_;

typedef std::function<void(uint8_t*, int)> AudioCallback;

class EngineBase
{
public:
	EngineBase();
	~EngineBase();

private:
	static EngineBase* this_;

	//初始化类函数
private:
	_image logo = NULL;
	unsigned int clLogoBG = 0x200520;
	unsigned int clBG = 0x200520;

	std::mutex textureMutex;
	std::mutex soundMutex;
	SDL_Window * window = NULL;
	SDL_Renderer * renderer = NULL;
	SDL_Texture * realScreen = NULL;
	int windowWidth;
	int windowHeight;
	InitErrorType initSDL(const std::string& windowCaption, int wWidth, int wHeight, bool isFullScreen);
	void destroySDL();

public:
	InitErrorType initEngineBase(const std::string& windowCaption, int wWidth, int wHeight, bool isFullScreen);
	void destroyEngineBase();
	int width;
	int height;
	bool fullScreen;
	bool setFullScreen(bool full);
	void setWindowSize(int w, int h);
	bool hardwareCursor = true;

	//底层处理函数
private:
	Rect rect;
	void clearScreen();
	void displayScreen();
	void updateState();
	void updateRect();

private:
	void * lzoMem = NULL;
public:
	void * getMem(int size);
	void freeMem(void * mem);
	int getLZOOutLen(int inLen);
	int lzoCompress(const void * src, unsigned int srcLen, void * dst, unsigned int * dstLen);
	int lzoDecompress(const void * src, unsigned int srcLen, void * dst, unsigned int * dstLen);

	//时间类函数
private:
	Time time;
	int FPS = 0;
	int FPSTime = 0;
	int FPSCounter = 0;
	unsigned int initTime();
	void setTimePaused(bool paused);
	void countFPS();
public:
	unsigned int getTime();
	unsigned int initTime(Time * t);
	unsigned int getTime(Time * t);
	void setTime(Time * t, unsigned int time);
	void setTimePaused(Time * t, bool paused);
	void delay(unsigned int t);
	int getFPS();

	//鼠标样式的处理函数
private: 
	MouseImage mouseImage;
	int MouseImageIndex = -1;
	void clearMouse();
	void drawMouse();
	void updateMouse();
	void destroyMouse();
public:
	_cursor loadCursorFromMem(char * data, int size, int x, int y);
	void setMouse(MouseImage * mouse);
	void showMouse();
	void hideMouse();
	bool mouseHide = false;

public:
//图片相关的函数
	void drawImage(_image image, SDL_Rect * src, SDL_Rect * dst);
	void drawImage(_image image, SDL_Rect * rect);
	bool pointInImage(_image image, int x, int y);
	_image createNewImageFromImage(_image image);
	_image loadImageFromMem(char * data, int size);
	_image loadImageFromFile(const std::string & fileName);
	int saveImageToFile(_image image, int w, int h, const std::string & fileName);
	int saveImageToFile(_image image, const std::string & fileName);
	int saveImageToMem(_image image, int w, int h, char ** data);
	int saveImageToMem(_image image, char ** data);
	_image loadSurfaceFromMem(char * data, int size);
	void drawImage(_image image, int x, int y);
	void drawImage(_image image, Rect * rect);
	void drawImage(_image image, Rect *src, Rect * dst);
	void freeImage(_image image);
	void setImageAlpha(_image image, unsigned char a);
	void setImageColorMode(_image image, unsigned char r, unsigned char g, unsigned char b);
	void drawImageWithColor(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b);
	int getImageSize(_image image, int * w, int * h);

private:
	//used by save screen
	_image originalTex = NULL;
public:
	_image beginDrawTalk(int w, int h);
	_image endDrawTalk();
	void drawSolidUnicodeText(const std::wstring& text, int x, int y, int size, unsigned int color);

	_image createBMP16(int w, int h, char * data, int size);
	_image beginSaveScreen();
	_image endSaveScreen();
	int saveImageToBMP16(_image image, int w, int h, char ** s);

	_image createRaindrop();
	_image createSnowflake();

	void loadLogo();
	void fadeInLogo();
	void fadeOutLogo();

	//图像遮罩相关的函数
private:
	_image screenMask;
public:
#define LUM_MASK_WIDTH 800
#define LUM_MASK_HEIGHT 400
#define LUM_MASK_MAX_ALPHA 80
	_image createMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	_image createLumMask();
	//绘制遮罩
	void drawMask(_image mask);
	void drawMask(_image mask, Rect* dst);
	//图片与遮罩混合
	void drawImageWithMask(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMask(_image image, int x, int y, _image mask);
	void drawImageWithMask(_image image, Rect *src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMask(_image image, Rect *src, Rect * dst, _image mask);
	//在图片上层覆盖遮罩
	void drawImageWithMaskEx(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMaskEx(_image image, int x, int y, _image mask);
	void drawImageWithMaskEx(_image image, Rect *src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMaskEx(_image image, Rect *src, Rect * dst, _image mask);
	
	void setScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawScreenMask();

	//事件、鼠标位置等函数
private:
	int mousePosX;
	int mousePosY;
	EventList eventList;
	void handleEvent();
	void copyEvent(AEvent* s, AEvent* d);
	void clearEventList();
public:
	int mouseX;
	int mouseY;
	int getEventCount();
	int getEvent(AEvent * event);
	void pushEvent(AEvent * event);
	int readEventList(EventList * eList);
	bool getKeyPress(KeyCode key);
	bool getMousePress(MouseButtonCode button);
	void getMouse(int * x, int * y);
	void resetEvent();
	void setMouseHardWare(bool isHardware);

	//字符串显示相关的函数
private:
	std::string font = "";
	SDL_RWops * fontData = NULL;
	char * fontBuffer = NULL;
public:
	void setFontFromMem(void * data, int size);
	_image createUnicodeText(const std::wstring& text, int size, unsigned int color);
	void drawUnicodeText(const std::wstring& text, int x, int y, int size, unsigned int color);
	_image createText(const std::string& text, int size, unsigned int color);
	void drawText(const std::string& text,int x, int y, int size, unsigned int color);
	void setFontName(const std::string& fontName);
	

	//FMOD音频函数
private:
	int initSoundSystem();
	void destroySoundSystem();
	void updateSoundSystem();
	FMOD_SYSTEM * soundSystem;
	int maxChannel = 512;

	SoundAutoReleaseList soundList;
	static FMOD_RESULT F_CALLBACK autoReleaseSound(FMOD_CHANNELCONTROL *chanControl, FMOD_CHANNELCONTROL_TYPE controlType, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType, void *commandData1, void *commandData2);
public:
	_music createMusic(char * data, int size, bool loop, bool music3d, unsigned char priority = 128);
	void freeMusic(_music music);
	_channel playMusic(_music music, float volume);
	_channel playMusic(_music music, float x, float y, float volume);
	void stopMusic(_channel channel);
	void pauseMusic(_channel channel);
	void resumeMusic(_channel channel);
	void setMusicPosition(_channel channel, float x, float y);
	void setMusicVolume(_channel channel, float volume);
	bool getMusicPlaying(_channel channel);
	bool soundAutoRelease(_music music, _channel channel);

	//FFMPEG视频函数
private:
	const int videoConvertSize = 192000;
	int initVideo();
	void destroyVideo();
	void freeMediaStream(MediaStream * mediaStream);
	
	//将所有创建的video在创建时自动加入videoList列表中
	//在声音回调函数触发时检查列表中所有的video，得到声音所属video中的下一帧audio数据
	std::vector<video_> videoList;
	void clearVideoList();
	void addVideoToList(_video video);
	void deleteVideoFromList(_video video);
	void deleteVideoFromList(int index);
	
	static FMOD_RESULT F_CALLBACK audioCallback(FMOD_CHANNELCONTROL *chanControl, FMOD_CHANNELCONTROL_TYPE controlType, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType, void *commandData1, void *commandData2);
	static int convert(AVCodecContext* codecCtx, AVFrame* frame, int out_sample_format, int out_sample_rate, int out_channels, uint8_t* out_buf);

	int openVideoFile(_video video);
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
	
	void tryDecodeVideo(_video video);

	_music createVideoRAW(FMOD_SYSTEM * system, char * data, int size, bool loop, bool music3d, FMOD_SOUND_FORMAT format, int numchannels, int defaultfrequency, unsigned char priority = 128);

public:
	_video createNewVideo(const std::string& fileName);
	void setVideoRect(_video video, Rect * rect);
	void freeVideo(_video video);
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
	//每一帧开始和结尾处的处理，必须调用
public:	
	void frameBegin();
	void frameEnd();

};