#include "Engine.h"
#include "../Image/IMP.h"

Engine Engine::engine;
Engine* Engine::_this = &Engine::engine;

Engine::Engine()
{
}

Engine::~Engine()
{
	destroyEngine();
}

int Engine::engineAppEventHandler(SDL_Event* event)
{
	switch (event->type)
	{
	case SDL_APP_TERMINATING:
		/* Terminate the app.
			Shut everything down before returning from this function.
		*/
		return 0;
	case SDL_APP_LOWMEMORY:
		/* You will get this when your app is paused and iOS wants more memory.
			Release as much memory as possible.
		*/
		return 0;
	case SDL_APP_WILLENTERBACKGROUND:
		/* Prepare your app to go into the background.  Stop loops, etc.
			This gets called when the user hits the home button, or gets a call.
		*/
		getInstance()->stopBGM();
		return 0;
	case SDL_APP_DIDENTERBACKGROUND:
		/* This will get called if the user accepted whatever sent your app to the background.
			If the user got a phone call and canceled it, you'll instead get an SDL_APP_DIDENTERFOREGROUND event and restart your loops.
			When you get this, you have 5 seconds to save all your state or the app will be terminated.
			Your app is NOT active at this point.
		*/
		return 0;
	case SDL_APP_WILLENTERFOREGROUND:
		/* This call happens when your app is coming back to the foreground.
			Restore all your state here.
		*/
		getInstance()->resumeBGM();
		return 0;
	case SDL_APP_DIDENTERFOREGROUND:
		/* Restart your loops here.
			Your app is interactive and getting CPU again.
		*/
		return 0;
	default:
		/* No special processing, add it to the event queue */
		return 1;
	}
}

Engine* Engine::getInstance()
{
	return _this;
}

int Engine::init(std::string & windowCaption, int windowWidth, int windowHeight, bool isFullScreen)
{
	fullScreen = isFullScreen;
	width = windowWidth;
	height = windowHeight;

	if (EngineBase::initEngineBase(windowCaption, windowWidth, windowHeight, isFullScreen, engineAppEventHandler) != initOK)
	{
		return initError;
	}

#ifdef _MOBILE
    width = EngineBase::width;
    height = EngineBase::height;
#endif

	return initOK;
}

void Engine::destroyEngine()
{
	if (bgm != nullptr)
	{
		freeMusic(bgm);
		bgm = nullptr;
	}
	if (talk != nullptr)
	{
		freeMusic(talk);
		talk = nullptr;
	}
    //EngineBase::destroyEngineBase();
}

void Engine::getWindowSize(int& w, int& h)
{
	w = width;
	h = height;
}

void Engine::setWindowSize(int w, int h)
{
	if (w < 0 || h < 0)
	{
		return;
	}
	width = w;
	height = h;
	EngineBase::setWindowSize(w, h);
}

bool Engine::setWindowFullScreen(bool full)
{
	return fullScreen = EngineBase::setFullScreen(full);
}

bool Engine::setWindowDisplayMode(bool dm)
{
	return EngineBase::setDisplayMode(dm);
}

void Engine::getScreenInfo(int& w, int& h)
{
	EngineBase::getScreenInfo(w, h);
}

bool Engine::getWindowFullScreen()
{
	return fullScreen;
}

_shared_image Engine::loadImageFromFile(const std::string & fileName)
{
	return EngineBase::loadImageFromFile(fileName);
}

_shared_image Engine::loadImageFromMem(std::unique_ptr<char[]>& data, int size)
{
	if (data == nullptr || size <= 0)
	{
		return nullptr;
	}
	return EngineBase::loadImageFromMem(data, size);	
}

int Engine::saveImageToFile(_shared_image image, int w, int h, const std::string & fileName)
{
	return EngineBase::saveImageToFile(image, w, h, fileName);
}

int Engine::saveImageToFile(_shared_image image, const std::string & fileName)
{
	return EngineBase::saveImageToFile(image, fileName);
}

int Engine::saveImageToMem(_shared_image image, int w, int h, std::unique_ptr<char[]>& data)
{
	return EngineBase::saveImageToMem(image, w, h, data);
}

int Engine::saveImageToMem(_shared_image image, std::unique_ptr<char[]>& data)
{
	return EngineBase::saveImageToMem(image, data);
}

bool Engine::pointInImage(_shared_image image, int x, int y)
{
	return EngineBase::pointInImage(image, x, y);
}

_shared_image Engine::createNewImageFromImage(_shared_image image)
{
	return EngineBase::createNewImageFromImage(image);
}

void Engine::setMouseFromImpImage(_shared_imp impImage)
{
    if (impImage == nullptr)
    {
        EngineBase::setCursor(nullptr);
        return;
    }
	CursorImage mouse;
	mouse.interval = impImage->interval;
	mouse.image.resize(impImage->frame.size());

	for (size_t i = 0; i < mouse.image.size(); i++)
	{
		mouse.image[i].xOffset = 0;//impImage->frame[i].xOffset;
		mouse.image[i].yOffset = 0;// impImage->frame[i].yOffset;
		if (impImage->frame[i].dataLen > 0)
		{
			mouse.image[i].frame = EngineBase::loadCursorFromMem(impImage->frame[i].data, impImage->frame[i].dataLen, mouse.image[i].xOffset, mouse.image[i].yOffset);
			mouse.image[i].softwareFrame = EngineBase::loadImageFromMem(impImage->frame[i].data, impImage->frame[i].dataLen);
			impImage->frame[i].image = EngineBase::loadImageFromMem(impImage->frame[i].data, impImage->frame[i].dataLen);
			impImage->frame[i].data = nullptr;
			impImage->frame[i].dataLen = 0;
			
		}
		else
		{
			mouse.image[i].frame = nullptr;
			mouse.image[i].softwareFrame = nullptr;
		}
	}

	EngineBase::setCursor(&mouse);

	mouse.image.resize(0);
}

void Engine::setMouseHardware(bool hdCursor)
{
	EngineBase::setCursorHardware(hdCursor);
}

bool Engine::getMouseHardware()
{
	return EngineBase::hardwareCursor;
}

void Engine::showCursor()
{
	EngineBase::showCursor();
}

void Engine::hideCursor()
{
	EngineBase::hideCursor();
}

int Engine::getEventCount()
{
	return EngineBase::getEventCount();
}

int Engine::getEvent(AEvent& event)
{
	return EngineBase::getEvent(event);
}

void Engine::pushEvent(AEvent& event)
{
	EngineBase::pushEvent(event);
}

int Engine::readEventList(EventList& eList)
{
	return EngineBase::readEventList(eList);
}

bool Engine::getKeyPress(KeyCode key)
{
	return EngineBase::getKeyPress(key);
}

bool Engine::getMousePressed(MouseButtonCode button)
{
	return EngineBase::getMousePress(button);
}

void Engine::getMousePosition(int& x, int& y)
{
	EngineBase::getMouse(x, y);
}

void Engine::frameBegin()
{
	EngineBase::frameBegin();
}

void Engine::frameEnd()
{
	drawFPS();
	EngineBase::frameEnd();
}

UTime Engine::getTime()
{
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
#ifdef _CONSOLE_MODE
#define _DRAW_FPS
#endif // _CONSOLE_MODE

#ifdef _DRAW_FPS
	//std::string s = convert::formatString("%dfps,%0.3fs,%d,%d", getFPS(),((float)getAbsoluteTime())/ 1000, x, y);
	std::string s = convert::formatString("FPS:%d", getFPS());
	drawText(s, 0, 0, 25, 0xD0FFFFFF);
#endif // _DRAW_FPS
}

void Engine::drawImage(_shared_image image, int x, int y)
{
	EngineBase::drawImage(image, x, y);
}

void Engine::drawImage(_shared_image image, Rect* src, Rect* dst)
{
	EngineBase::drawImage(image, src, dst);
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
	EngineBase::setImageColorMode(image, r, g, b);
}

//void Engine::freeImage(_image image)
//{
//	EngineBase::freeImage(image);
//}

int Engine::getImageSize(_shared_image image, int &w, int &h)
{
	return EngineBase::getImageSize(image, w, h);
}

bool Engine::beginDrawTalk(int w, int h)
{
	return EngineBase::beginDrawTalk(w, h);
}

_shared_image Engine::endDrawTalk()
{
	return EngineBase::endDrawTalk();
}

void Engine::drawSolidText(const std::string& text, int x, int y, int size, unsigned int color)
{
	EngineBase::drawSolidText(text, x, y, size, color);
}

void Engine::drawSolidUnicodeText(const std::wstring & text, int x, int y, int size, unsigned int color)
{
	EngineBase::drawSolidUnicodeText(text, x, y, size, color);
}

_shared_image Engine::loadSaveShotFromPixels(int w, int h, std::unique_ptr<char[]>& data, int size)
{
	return EngineBase::loadSaveShotFromPixels(w, h, data, size);
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
	return EngineBase::saveImageToPixels(image, w, h, s);
}

_shared_image Engine::createRaindrop()
{
	return EngineBase::createRaindrop();
}

_shared_image Engine::createSnowflake()
{
	return EngineBase::createSnowflake();
}

void Engine::setFontName(const std::string & fontName)
{
	EngineBase::setFontName(fontName);
}

void Engine::setFontFromMem(std::unique_ptr<char[]>& data, int size)
{
	EngineBase::setFontFromMem(data, size);
}

_shared_image Engine::createText(const std::string & text, int size, unsigned int color)
{
	return EngineBase::createText(text, size, color);
}

void Engine::drawText(const std::string & text, int x, int y, int size, unsigned int color)
{
	EngineBase::drawText(text, x, y, size, color);
}

_shared_image Engine::createUnicodeText(const std::wstring& text, int size, unsigned int color)
{
	return EngineBase::createUnicodeText(text, size, color);
}

void Engine::drawUnicodeText(const std::wstring& text, int x, int y, int size, unsigned int color)
{
	EngineBase::drawUnicodeText(text, x, y, size, color);
}

void Engine::setImageAlpha(_shared_image image, unsigned char a)
{
	EngineBase::setImageAlpha(image, a);
}

void Engine::setScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	EngineBase::setScreenMask(r, g, b, a);
}

void Engine::drawScreenMask()
{
	EngineBase::drawScreenMask();
}

void Engine::drawScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	setScreenMask(r, g, b, a);
	drawScreenMask();
}

_shared_image Engine::createMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	return EngineBase::createMask(r, g, b, a);
}

_shared_image Engine::createLumMask()
{
	return EngineBase::createLumMask();
}

//绘制遮罩
void Engine::drawMask(_shared_image mask)
{
	EngineBase::drawMask(mask);
}

void Engine::drawMask(_shared_image mask, Rect * dst)
{
	EngineBase::drawMask(mask, dst);
}

//绘制带遮罩的
void Engine::drawImageWithMask(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	EngineBase::drawImageWithMask(image, x, y, r, g, b, a);
}

void Engine::drawImageWithMask(_shared_image image, int x, int y, _shared_image mask)
{
	EngineBase::drawImageWithMask(image, x, y, mask);
}

void Engine::drawImageWithMask(_shared_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	EngineBase::drawImageWithMask(image, src, dst, r, g, b, a);
}

void Engine::drawImageWithMask(_shared_image image, Rect * src, Rect * dst, _shared_image mask)
{
	EngineBase::drawImageWithMask(image, src, dst, mask);
}

void Engine::drawImageWithMaskEx(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	EngineBase::drawImageWithMaskEx(image, x, y, r, g, b, a);
}

void Engine::drawImageWithMaskEx(_shared_image image, int x, int y, _shared_image mask)
{
	EngineBase::drawImageWithMaskEx(image, x, y, mask);
}

void Engine::drawImageWithMaskEx(_shared_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	EngineBase::drawImageWithMaskEx(image, src, dst, r, g, b, a);
}

void Engine::drawImageWithMaskEx(_shared_image image, Rect * src, Rect * dst, _shared_image mask)
{
	EngineBase::drawImageWithMaskEx(image, src, dst, mask);
}

void * Engine::getMem(int size)
{
	return EngineBase::getMem(size);
}

void Engine::freeMem(void * mem)
{
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
	int size;
	if (!File::readFile(fileName, data, size))
	{
		printf("Music File %s Readed Error\n", fileName.c_str());
		return nullptr;
	}
	if (data == nullptr || size <= 0)
	{
		printf("Music File %s Readed Error\n", fileName.c_str());
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

_channel Engine::playSound(const std::unique_ptr<char[]>& data, int size, float x, float y, float volume)
{
	_music m = loadSound(data, size);
	if (m == nullptr)
	{
		return nullptr;
	}
	if (volume < 0)
	{
		volume = soundVolume;
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
	}
	bgm = createMusic(fileName, true, false, 0);
	return bgm;
}

void Engine::setBGMVolume(float volume)
{
	bgmVolume = volume;
	EngineBase::setMusicVolume(channelBGM, bgmVolume);
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
	return EngineBase::loadVideo(fileName);
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

double Engine::getVideoTime(_video v)
{
	return EngineBase::getVideoTime(v);
}
