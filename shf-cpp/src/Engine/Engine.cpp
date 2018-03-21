#include "Engine.h"

Engine Engine::engine;
Engine* Engine::this_ = &Engine::engine;

Engine::Engine()
{
	engineBase = new EngineBase;
}

Engine::~Engine()
{
	destroyEngine();
}

Engine * Engine::getInstance()
{
	return this_;
}

int Engine::init(std::string & windowCaption, int windowWidth, int windowHeight, bool isFullScreen)
{
	fullScreen = isFullScreen;
	width = windowWidth;
	height = windowHeight;

	if (engineBase->initEngineBase(windowCaption, windowWidth, windowHeight, isFullScreen) != initOK)
	{
		return initError;
	}

	initTime();

	return initOK;
}

void Engine::destroyEngine()
{
	if (bgm != NULL)
	{
		freeMusic(bgm);
		bgm = NULL;
	}
	if (talk != NULL)
	{
		freeMusic(talk);
		talk = NULL;
	}
	if (engineBase != NULL)
	{
		delete engineBase;
		engineBase = NULL;
	}
}

void Engine::getWindowSize(int * w, int * h)
{
	if (w != NULL)
	{
		*w = width;
	}
	if (h != NULL)
	{
		*h = height;
	}
}

void Engine::setWindowSize(int w, int h)
{
	if (w < 0 || h < 0)
	{
		return;
	}
	width = w;
	height = h;
	engineBase->setWindowSize(w, h);
}

bool Engine::setWindowFullScreen(bool full)
{
	return fullScreen = engineBase->setFullScreen(full);
}

bool Engine::getWindowFullScreen()
{
	return fullScreen;
}

_image Engine::loadImageFromFile(const std::string & fileName)
{
	return engineBase->loadImageFromFile(fileName);
}

void * Engine::loadImageFromMem(char * data, int size)
{
	if (data == NULL || size <= 0)
	{
		return NULL;
	}
	return engineBase->loadImageFromMem(data, size);	
}

int Engine::saveImageToFile(_image image, int w, int h, const std::string & fileName)
{
	return engineBase->saveImageToFile(image, w, h, fileName);
}

int Engine::saveImageToFile(_image image, const std::string & fileName)
{
	return engineBase->saveImageToFile(image, fileName);
}

int Engine::saveImageToMem(_image image, int w, int h, char ** data)
{
	return engineBase->saveImageToMem(image, w, h, data);
}

int Engine::saveImageToMem(_image image, char ** data)
{
	return engineBase->saveImageToMem(image, data);
}

bool Engine::pointInImage(_image image, int x, int y)
{
	return engineBase->pointInImage(image, x, y);
}

_image Engine::createNewImageFromImage(_image image)
{
	return engineBase->createNewImageFromImage(image);
}

void Engine::setMouseFromImpImage(IMPImage* impImage)
{
	if (impImage == NULL)
	{
		return;
	}
	MouseImage mouse;
	mouse.interval = impImage->interval;
	mouse.imageCount = impImage->frameCount;
	mouse.image.resize(mouse.imageCount);
	for (int i = 0; i < mouse.imageCount; i++)
	{
		mouse.image[i].xOffset = 0;//impImage->frame[i].xOffset;
		mouse.image[i].yOffset = 0;// impImage->frame[i].yOffset;
		if (impImage->frame[i].dataLen > 0)
		{
			mouse.image[i].frame = engineBase->loadCursorFromMem(&(impImage->frame[i].data[0]), impImage->frame[i].dataLen, mouse.image[i].xOffset, mouse.image[i].yOffset);
			mouse.image[i].softFrame = engineBase->loadImageFromMem(&(impImage->frame[i].data[0]), impImage->frame[i].dataLen);
			impImage->frame[i].image = engineBase->loadImageFromMem(&(impImage->frame[i].data[0]), impImage->frame[i].dataLen);
			delete[] impImage->frame[i].data;
			impImage->frame[i].data = NULL;
			impImage->frame[i].dataLen = 0;
		}
		else
		{
			mouse.image[i].frame = NULL;
			mouse.image[i].softFrame = NULL;
		}
	}

	engineBase->setMouse(&mouse);

	mouse.image.resize(0);
}

void Engine::setMouseHardware(bool hdCursor)
{
	engineBase->setMouseHardWare(hdCursor);
}

bool Engine::getMouseHardware()
{
	return engineBase->hardwareCursor;
}

void Engine::showMouse()
{
	engineBase->showMouse();
}

void Engine::hideMouse()
{
	engineBase->hideMouse();
}

int Engine::getEventCount()
{
	return engineBase->getEventCount();
}

int Engine::getEvent(AEvent * event)
{
	return engineBase->getEvent(event);
}

void Engine::pushEvent(AEvent * event)
{
	engineBase->pushEvent(event);
}

int Engine::readEventList(EventList * eList)
{
	return engineBase->readEventList(eList);
}

bool Engine::getKeyPress(KeyCode key)
{
	return engineBase->getKeyPress(key);
}

bool Engine::getMousePress(MouseButtonCode button)
{
	return engineBase->getMousePress(button);
}

void Engine::getMouse(int * x, int * y)
{
	engineBase->getMouse(x, y);
}

void Engine::frameBegin()
{
	engineBase->frameBegin();
}

void Engine::frameEnd()
{
	drawFPS();
	engineBase->frameEnd();
}

unsigned int Engine::getAbsoluteTime()
{
	return engineBase->getTime();
}

unsigned int Engine::initTime()
{
	return engineBase->initTime(&time);
}

unsigned int Engine::getTime()
{
	return engineBase->getTime(&time);
}

void Engine::setTimePaused(bool paused)
{
	engineBase->setTimePaused(&time, paused);
}

unsigned int Engine::initTime(Time * t)
{
	unsigned int now = 0;
	if (t->parent != NULL)
	{
		now = getTime(t->parent);
	}
	else
	{
		now = getTime();
	}
	t->beginTime = now;
	t->paused = false;
	return t->beginTime;
}

unsigned int Engine::getTime(Time * t)
{
	if (t->paused)
	{
		return t->pauseBeginTime - t->beginTime;
	}
	else
	{
		unsigned int now = 0;
		if (t->parent != NULL)
		{
			now = getTime(t->parent);
		}
		else
		{
			now = getTime();
		}
		return now - t->beginTime;
	}
}

void Engine::setTimePaused(Time * t, bool paused)
{
	if (paused == t->paused)
	{
		return;
	}
	unsigned int now = 0;
	if (t->parent != NULL)
	{
		now = getTime(t->parent);
	}
	else
	{
		now = getTime();
	}
	if (paused)
	{
		t->paused = true;
		t->pauseBeginTime = now;
	}
	else
	{
		t->paused = false;
		t->beginTime += now - t->pauseBeginTime;
	}
}

unsigned int Engine::setTime(Time * t, unsigned int time)
{
	if (t->paused)
	{
		t->beginTime = t->pauseBeginTime - time;
	}
	else
	{
		unsigned int now = 0;
		if (t->parent != NULL)
		{
			now = getTime(t->parent);
		}
		else
		{
			now = getTime();
		}
		t->beginTime = now - time;
	}
	return getTime(t);
}

void Engine::delay(unsigned int t)
{
	engineBase->delay(t);
}

int Engine::getFPS()
{
	return engineBase->getFPS();
}

void Engine::drawFPS()
{
#ifdef _CONSOLE_MODE
#define _DRAW_FPS
#endif // _CONSOLE_MODE
#ifdef _DRAW_FPS
	int x, y;
	getMouse(&x, &y);
	//std::string s = convert::formatString("%dfps,%0.3fs,%d,%d", getFPS(),((float)getAbsoluteTime())/ 1000, x, y);
	std::string s = convert::formatString("FPS:%d", getFPS());
	drawText(s, 0, 0, 25, 0xD0FFFFFF);
#endif // _DRAW_FPS
}

void Engine::drawImage(_image image, int x, int y)
{
	engineBase->drawImage(image, x, y);
}

void Engine::drawImage(_image image, Rect* src, Rect* dst)
{
	engineBase->drawImage(image, src, dst);
}

void Engine::drawImageWithAlpha(_image image, int x, int y, unsigned char alpha)
{
	setImageAlpha(image, alpha);
	drawImage(image, x, y);
	setImageAlpha(image, 255);
}

void Engine::drawImageWithAlpha(_image image, Rect * src, Rect * dst, unsigned char alpha)
{
	setImageAlpha(image, alpha);
	drawImage(image, src, dst);
	setImageAlpha(image, 255);
}

void Engine::drawImageWithColor(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
	engineBase->drawImageWithColor(image, x, y, r, g, b);
}

void Engine::drawImageWithColor(_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b)
{
	setImageColorMode(image, r, g, b);
	drawImage(image, src, dst);
	setImageColorMode(image, 255, 255, 255);
}

void Engine::setImageColorMode(_image image, unsigned char r, unsigned char g, unsigned char b)
{
	engineBase->setImageColorMode(image, r, g, b);
}

void Engine::freeImage(_image image)
{
	engineBase->freeImage(image);
}

int Engine::getImageSize(_image image, int * w, int * h)
{
	return engineBase->getImageSize(image, w, h);
}

_image Engine::beginDrawTalk(int w, int h)
{
	return engineBase->beginDrawTalk(w, h);
}

_image Engine::endDrawTalk()
{
	return engineBase->endDrawTalk();
}

void Engine::drawSolidUnicodeText(const std::wstring & text, int x, int y, int size, unsigned int color)
{
	engineBase->drawSolidUnicodeText(text, x, y, size, color);
}

_image Engine::createBMP16(int w, int h, char * data, int size)
{
	return engineBase->createBMP16(w, h, data, size);
}

_image Engine::beginSaveScreen()
{
	return engineBase->beginSaveScreen();
}

_image Engine::endSaveScreen()
{
	return engineBase->endSaveScreen();
}

int Engine::saveImageToBMP16(_image image, int w, int h, char ** s)
{
	return engineBase->saveImageToBMP16(image, w, h, s);
}

_image Engine::createRaindrop()
{
	return engineBase->createRaindrop();
}

_image Engine::createSnowflake()
{
	return engineBase->createSnowflake();
}

void Engine::setFontName(const std::string & fontName)
{
	engineBase->setFontName(fontName);
}

void Engine::setFontFromMem(void * data, int size)
{
	engineBase->setFontFromMem(data, size);
}

_image Engine::createText(const std::string & text, int size, unsigned int color)
{
	return engineBase->createText(text, size, color);
}

void Engine::drawText(const std::string & text, int x, int y, int size, unsigned int color)
{
	engineBase->drawText(text, x, y, size, color);
}

_image Engine::createUnicodeText(const std::wstring& text, int size, unsigned int color)
{
	return engineBase->createUnicodeText(text, size, color);
}

void Engine::drawUnicodeText(const std::wstring& text, int x, int y, int size, unsigned int color)
{
	engineBase->drawUnicodeText(text, x, y, size, color);
}

void Engine::setImageAlpha(_image image, unsigned char a)
{
	engineBase->setImageAlpha(image, a);
}

void Engine::setScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	engineBase->setScreenMask(r, g, b, a);
}

void Engine::drawScreenMask()
{
	engineBase->drawScreenMask();
}

void Engine::drawScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	setScreenMask(r, g, b, a);
	drawScreenMask();
}

_image Engine::createMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	return engineBase->createMask(r, g, b, a);
}

_image Engine::createLumMask()
{
	return engineBase->createLumMask();
}

//绘制遮罩
void Engine::drawMask(_image mask)
{
	engineBase->drawMask(mask);
}

void Engine::drawMask(_image mask, Rect * dst)
{
	engineBase->drawMask(mask, dst);
}

//绘制带遮罩的
void Engine::drawImageWithMask(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	engineBase->drawImageWithMask(image, x, y, r, g, b, a);
}

void Engine::drawImageWithMask(_image image, int x, int y, _image mask)
{
	engineBase->drawImageWithMask(image, x, y, mask);
}

void Engine::drawImageWithMask(_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	engineBase->drawImageWithMask(image, src, dst, r, g, b, a);
}

void Engine::drawImageWithMask(_image image, Rect * src, Rect * dst, _image mask)
{
	engineBase->drawImageWithMask(image, src, dst, mask);
}

void Engine::drawImageWithMaskEx(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	engineBase->drawImageWithMaskEx(image, x, y, r, g, b, a);
}

void Engine::drawImageWithMaskEx(_image image, int x, int y, _image mask)
{
	engineBase->drawImageWithMaskEx(image, x, y, mask);
}

void Engine::drawImageWithMaskEx(_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	engineBase->drawImageWithMaskEx(image, src, dst, r, g, b, a);
}

void Engine::drawImageWithMaskEx(_image image, Rect * src, Rect * dst, _image mask)
{
	engineBase->drawImageWithMaskEx(image, src, dst, mask);
}

void * Engine::getMem(int size)
{
	return engineBase->getMem(size);
}

void Engine::freeMem(void * mem)
{
	engineBase->freeMem(mem);
}

int Engine::getLZOOutLen(int inLen)
{
	return engineBase->getLZOOutLen(inLen);
}

int Engine::lzoCompress(const void * src, unsigned int srcLen, void * dst, unsigned int * dstLen)
{
	return engineBase->lzoCompress(src, srcLen, dst, dstLen);
}

int Engine::lzoDecompress(const void * src, unsigned int srcLen, void * dst, unsigned int * dstLen)
{
	return engineBase->lzoDecompress(src, srcLen, dst, dstLen);
}

_music Engine::createMusic(char * data, int size, bool loop, bool music3d, unsigned char priority)
{
	return engineBase->createMusic(data, size, loop, music3d, priority);
}

_music Engine::createMusic(const std::string & fileName, bool loop, bool music3d, unsigned char priority)
{
	char * data;
	int size;
	if (!File::readFile(fileName, &data, &size))
	{
		printf("Music File %s Readed Error\n", fileName.c_str());
		return NULL;
	}
	if (data == NULL || size <= 0)
	{
		printf("Music File %s Readed Error\n", fileName.c_str());
		return NULL;
	}
	_music result = createMusic(data, size, loop, music3d, priority);
	delete[] data;
	return result;
}

void Engine::freeMusic(_music music)
{
	engineBase->freeMusic(music);
}

void Engine::setMusicPosition(_channel channel, float x, float y)
{
	engineBase->setMusicPosition(channel, x, y);
}

void Engine::setMusicVolume(_channel channel, float volume)
{
	engineBase->setMusicVolume(channel, volume);
}

_channel Engine::playMusic(_music music, float volume)
{
	return engineBase->playMusic(music, volume);
}

_channel Engine::playMusic(_music music, float x, float y, float volume)
{
	return engineBase->playMusic(music, x, y, volume);
}

void Engine::stopMusic(_channel channel)
{
	engineBase->stopMusic(channel);
}

void Engine::pauseMusic(_channel channel)
{
	engineBase->pauseMusic(channel);
}

void Engine::resumeMusic(_channel channel)
{
	engineBase->resumeMusic(channel);
}

bool Engine::getMusicPlaying(_channel channel)
{
	return engineBase->getMusicPlaying(channel);
}

bool Engine::soundAutoRelease(_music music, _channel channel)
{
	return engineBase->soundAutoRelease(music, channel);
}

_music Engine::loadSound(char * data, int size)
{
	return createMusic(data, size, false, true);
}

_music Engine::loadSound(const std::string & fileName)
{
	return createMusic(fileName, false, true);
}

_music Engine::loadCircleSound(char * data, int size)
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

_channel Engine::playSound(_music music, float x, float y)
{
	return playMusic(music, x, y, soundVolume);
}

_channel Engine::playSound(_music music)
{
	return playSound(music, 0, 0);
}

_channel Engine::playSound(char * data, int size, float x, float y)
{
	_music m = loadSound(data, size);
	if (m == NULL)
	{
		return NULL;
	}
	_channel c = playSound(m, x, y);
	if (c == NULL)
	{
		freeMusic(m);
		return NULL;
	}
	if (soundAutoRelease(m, c))
	{
		return c;
	}
	else
	{
		freeMusic(m);
		return NULL;
	}
}

_music Engine::loadBGM(char * data, int size)
{
	if (bgm != NULL)
	{
		freeMusic(bgm);
		bgm = NULL;
	}
	bgm = createMusic(data, size, true, false, 0);
	 return bgm;
}

_music Engine::loadBGM(const std::string & fileName)
{
	if (bgm != NULL)
	{
		freeMusic(bgm);
		bgm = NULL;
	}
	bgm = createMusic(fileName, true, false, 0);
	return bgm;
}

void Engine::setBGMVolume(float volume)
{
	bgmVolume = volume;
	engineBase->setMusicVolume(channelBGM, bgmVolume);
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
	engineBase->stopMusic(channelBGM);
}

void Engine::pauseBGM()
{
	engineBase->pauseMusic(channelBGM);
}

void Engine::resumeBGM()
{
	engineBase->resumeMusic(channelBGM);
}

_music Engine::loadTalk(char * data, int size)
{
	if (talk != NULL)
	{
		freeMusic(talk);
		talk = NULL;
	}
	talk = createMusic(data, size, false, false, 0);
	return talk;
}

_music Engine::loadTalk(const std::string & fileName)
{
	if (talk != NULL)
	{
		freeMusic(talk);
		talk = NULL;
	}
	talk = createMusic(fileName, false, false, 0);
	return talk;
}

void Engine::setTalkVolume(float volume)
{
	talkVolume = volume;
	engineBase->setMusicVolume(channelTalk, volume);
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
	engineBase->stopMusic(channelTalk);
}

void Engine::pauseTalk()
{
	engineBase->pauseMusic(channelTalk);
}

void Engine::resumeTalk()
{
	engineBase->resumeMusic(channelTalk);
}

_video Engine::createNewVideo(const std::string & fileName)
{
	return engineBase->createNewVideo(fileName);
}

void Engine::setVideoRect(_video v, Rect * rect)
{
	engineBase->setVideoRect(v, rect);
}

void Engine::freeVideo(_video v)
{
	engineBase->freeVideo(v);
}

void Engine::runVideo(_video v)
{
	engineBase->runVideo(v);
}

bool Engine::updateVideo(_video v)
{
	return engineBase->updateVideo(v);
}

void Engine::drawVideoFrame(_video v)
{
	engineBase->drawVideoFrame(v);
}

bool Engine::onVideoFrame(_video v)
{
	return engineBase->onVideoFrame(v);
}

void Engine::pauseVideo(_video v)
{
	engineBase->pauseVideo(v);
}

void Engine::resumeVideo(_video v)
{
	engineBase->resumeVideo(v);
}

void Engine::stopVideo(_video v)
{
	engineBase->stopVideo(v);
}

void Engine::resetVideo(_video v)
{
	engineBase->resetVideo(v);
}

void Engine::setVideoLoop(_video v, int loop)
{
	engineBase->setVideoLoop(v, loop);
}

bool Engine::getVideoStopped(_video v)
{
	return engineBase->getVideoStopped(v);
}

double Engine::getVideoTime(_video v)
{
	return engineBase->getVideoTime(v);
}
