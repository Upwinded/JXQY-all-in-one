/*
	Engine模块是位于EngineBase模块上层的引擎，供游戏实际调用。
	EngineBase封装了SDL、FMOD、FFMPEG等模块，实现了引擎大部分的底层功能。
	Engine模块是对EngineBase的再封装，提供了最主要的常用接口和一些EngineBase
	没写的专用接口（例如将声音播放区分为背景音乐、音效、对话等）。
*/

#pragma once

#include "EngineBase.h"

enum MusicType
{
	mtNONE = 0,
	mtMUSIC = 1,
	mtSOUND = 2,
	mtTALK = 4
};

class Engine
{

private:
	static Engine engine;
	static Engine * this_;

	EngineBase* engineBase;

	Time time;

	int width = 800;
	int height = 600;

	bool fullScreen = false;

public:
	Engine();
	~Engine();

	static Engine* getInstance();

	//初始化引擎
	int init(std::string & windowCaption, int windowWidth, int windowHeight, bool isFullScreen);
	//释放引擎
	void destroyEngine();
	//获取窗口尺寸
	void getWindowSize(int* w, int* h);
	//设置窗口尺寸
	void setWindowSize(int w, int h);
	//获取是否全屏
	bool getWindowFullScreen();
	//设置是否全屏
	bool setWindowFullScreen(bool full);
	//设置全屏显示模式，是否改变分辨率
	bool setWindowDisplayMode(bool dm);

	//设置鼠标样式
	void setMouseFromImpImage(IMPImage* impImage);

	void setMouseHardware(bool hdCursor);
	bool getMouseHardware();
	void showMouse();
	void hideMouse();
	
	//时间与定时器子程序
	//底层引擎会在处理事件时暂停计时器，此时移动窗体，改变窗体大小等事件会使主进程阻塞，
	//getAbsoluteTime()子程序会减掉事件处理的时间。
	//getTime()则会另外再令计算此引擎模块的暂停时间

	//得到绝对时间
	unsigned int getAbsoluteTime();
	//初始化engine内置定时器
	unsigned int initTime();
	//得到engine内置定时器的时间
	unsigned int getTime();	
	//暂停engine内置定时器
	void setTimePaused(bool paused);
	//以engine内置定时器为基准，初始化其它定时器
	unsigned int initTime(Time * t);
	//以engine内置定时器为基准，得到其它定时器的时间
	unsigned int getTime(Time * t);
	//以engine内置定时器为基准，暂停其它定时器
	void setTimePaused(Time * t, bool paused);
	//设置定时器时间
	unsigned int setTime(Time * t, unsigned int time);
	//系统延时（毫秒）
	void delay(unsigned int t);

	//得到帧率
	int getFPS();
	void drawFPS();

	//绘图子程序
	_image loadImageFromFile(const std::string & fileName);
	_image loadImageFromMem(char * data, int size);
	int saveImageToFile(_image image, int w, int h, const std::string & fileName);
	int saveImageToFile(_image image, const std::string & fileName);
	int saveImageToMem(_image image, int w, int h, char ** data);
	int saveImageToMem(_image image, char ** data);
	bool pointInImage(_image image, int x, int y);
	_image createNewImageFromImage(_image image);
	void drawImage(_image image, int x, int y);
	void drawImage(_image image, Rect* src, Rect* dst);
	void drawImageWithAlpha(_image image, int x, int y, unsigned char alpha);
	void drawImageWithAlpha(_image image, Rect *src, Rect * dst, unsigned char alpha);
	void drawImageWithColor(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b);
	void drawImageWithColor(_image image, Rect* src, Rect* dst, unsigned char r, unsigned char g, unsigned char b);
	void setImageColorMode(_image image, unsigned char r, unsigned char g, unsigned char b);
	void setImageAlpha(_image image, unsigned char a);
	void freeImage(_image image);
	int getImageSize(_image image, int * w, int * h);

	//绘制缩略图子程序
	_image createBMP16(int w, int h, char * data, int size);
	_image beginSaveScreen();
	_image endSaveScreen();
	int saveImageToBMP16(_image image, int w, int h, char ** s);

	//天气类图像
	_image createRaindrop();
	_image createSnowflake();
	
	//绘制文字
	void setFontName(const std::string& fontName);
	void setFontFromMem(void * data, int size);
	_image createText(const std::string& text, int size, unsigned int color);
	void drawText(const std::string& text, int x, int y, int size, unsigned int color);
	_image createUnicodeText(const std::wstring& text, int size, unsigned int color);
	void drawUnicodeText(const std::wstring& text, int x, int y, int size, unsigned int color);

	//保存字符串到图片子程序
	_image beginDrawTalk(int w, int h);
	_image endDrawTalk();
	void drawSolidUnicodeText(const std::wstring& text, int x, int y, int size, unsigned int color);

	//用于绘制屏幕遮盖，一般可用于夜晚、黄昏以及雷电等特殊情景的天色绘制
	void setScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawScreenMask();
	void drawScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a);

	//用于创建普通遮罩
	_image createMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	_image createLumMask();
	//绘制普通遮罩
	void drawMask(_image mask);
	void drawMask(_image mask, Rect * dst);
	//绘制带遮罩的图片
	void drawImageWithMask(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMask(_image image, int x, int y, _image mask);
	void drawImageWithMask(_image image, Rect *src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMask(_image image, Rect *src, Rect * dst, _image mask);
	
	void drawImageWithMaskEx(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMaskEx(_image image, int x, int y, _image mask);
	void drawImageWithMaskEx(_image image, Rect *src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void drawImageWithMaskEx(_image image, Rect *src, Rect * dst, _image mask);

	//压缩解压等子程序
	void * getMem(int size);
	void freeMem(void * mem);
	int getLZOOutLen(int inLen);
	int lzoCompress(const void * src, unsigned int srcLen, void * dst, unsigned int * dstLen);
	int lzoDecompress(const void * src, unsigned int srcLen, void * dst, unsigned int * dstLen);

	//事件子程序
	int getEventCount();
	int getEvent(AEvent * event);
	void pushEvent(AEvent * event);
	int readEventList(EventList * eList);
	bool getKeyPress(KeyCode key);
	bool getMousePress(MouseButtonCode button);
	void getMouse(int * x, int * y);
	
	//音频子程序
private:
	float bgmVolume = 1;
	float soundVolume = 1;
	float talkVolume = 1;

	_music bgm = NULL;
	_channel channelBGM = NULL;
	_music talk = NULL;
	_channel channelTalk = NULL;
public:

	//声音通用接口
	_music createMusic(char * data, int size, bool loop, bool music3d, unsigned char priority = 128);
	_music createMusic(const std::string & fileName, bool loop, bool music3d, unsigned char priority = 128);
	void freeMusic(_music music);
	void setMusicPosition(_channel channel, float x, float y);
	void setMusicVolume(_channel channel, float volume);
	_channel playMusic(_music music, float volume);
	_channel playMusic(_music music, float x, float y, float volume);
	void stopMusic(_channel channel);
	void pauseMusic(_channel channel);
	void resumeMusic(_channel channel);
	bool getMusicPlaying(_channel channel);
	bool soundAutoRelease(_music music, _channel channel);

	//声音专用接口

	//按照音效的默认设置来读取和播放声音
	_music loadSound(char * data, int size);
	_music loadSound(const std::string & fileName);
	//读取循环播放的音效
	_music loadCircleSound(char * data, int size);
	_music loadCircleSound(const std::string & fileName);
	void setSoundVolume(float volume);
	float getSoundVolume();
	_channel playSound(_music music, float x, float y);
	_channel playSound(_music music);

	//播放结束自动释放
	_channel playSound(char * data, int size, float x = 0.0f, float y = 0.0f);

	//按照背景音乐的默认设置来读取和播放声音
	_music loadBGM(char * data, int size);
	_music loadBGM(const std::string & fileName);
	void setBGMVolume(float volume);
	float getBGMVolume();
	_channel playBGM();
	void stopBGM();
	void pauseBGM();
	void resumeBGM();

	//按照对话配音的默认设置来读取和播放声音
	_music loadTalk(char * data, int size);
	_music loadTalk(const std::string & fileName);
	void setTalkVolume(float volume);
	float getTalkVolume();
	_channel playtalk();
	void stopTalk();
	void pauseTalk();
	void resumeTalk();
	
public:
	//根据文件名新建视频数据
	_video createNewVideo(const std::string& fileName);
	//设置视频显示区域，rect = NULL 时为全屏模式
	void setVideoRect(_video v, Rect * rect);
	//释放视频数据
	void freeVideo(_video v);

	//更新视频状态，解码视频音频等，应在每次循环中调用
	bool updateVideo(_video v);
	//绘制视频图像
	void drawVideoFrame(_video v);

	//此函数相当于依次调用：updateVideo(v);和drawVideoFrame(v);
	bool onVideoFrame(_video v); 

	//将视频设置为运行状态
	void runVideo(_video v);
	//暂停视频
	void pauseVideo(_video v);
	//恢复视频
	void resumeVideo(_video v);
	//停止视频
	void stopVideo(_video v);
	//从头播放视频
	void resetVideo(_video v);
	//设置视频循环播放，loop为循环次数，0为不循环，负数表示一直循环
	void setVideoLoop(_video v, int loop);
	//视频是否停止播放
	bool getVideoStopped(_video v);

	double getVideoTime(_video v);
public:
	//引擎每一帧都必须执行的处理函数
	//每帧开始前调用
	void frameBegin();
	//每帧结束后调用
	void frameEnd();

};