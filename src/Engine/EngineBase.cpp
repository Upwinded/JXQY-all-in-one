#include "EngineBase.h"
#include <map>
#include <iostream>
#include <thread>
#ifdef __ANDROID__
#include "../File/INIReader.h"
#include "../Game/GameTypes.h"
#endif

#include "stdio.h"
#include "Engine.h"

#ifdef SHF_USE_AUDIO
SoundAutoReleaseList EngineBase::soundList;
#ifdef SHF_USE_VIDEO
std::vector<_video> EngineBase::videoList;
#endif // SHF_USE_VIDEO
#endif // SHF_USE_AUDIO

std::mutex EngineBase::_mutex;
AppEventHandler EngineBase::_externalEventHandler = NULL;
std::atomic<SDL_Renderer*> EngineBase::renderer = nullptr;
std::atomic<SDL_Texture*>  EngineBase::tempRenderTarget = nullptr;
std::atomic<bool> EngineBase::isBackGround = false;
Timer EngineBase::timer;


Timer::Timer()
{
	reInit();
}

Timer::Timer(Timer* parent)
{
	setParent(parent);
	reInit();
}

Timer::~Timer()
{
	if (_parent != nullptr)
	{
		_parent->removeChild(this);
	}
	for (auto child: _children)
	{
		child->_parent = nullptr;
	}
	_children.clear();
}

void Timer::setParent(Timer* parent)
{
	auto now = getAbsolute() - _beginTime;
	if (_parent != nullptr)
	{
		_parent->removeChild(this);
	}
	_parent = parent;
	if (_parent != nullptr)
	{
		_parent->addChild(this);
	}
	_beginTime = getAbsolute() - now;
	//_paused = false;
}

UTime Timer::get()
{
	if (_paused)
	{
		return _pauseBeginTime - _beginTime;
	}
	else
	{
		return getAbsolute() - _beginTime;
	}
}

void Timer::set(UTime t)
{
	_beginTime += get() - t;
}

bool Timer::getPaused()
{
	return _paused;
}

void Timer::setPaused(bool paused)
{
	if (paused == _paused)
	{
		return;
	}
	if (paused)
	{
		_pauseBeginTime = getAbsolute();
		_paused = paused;
	}
	else
	{
		_paused = paused;
		_beginTime += getAbsolute() - _pauseBeginTime;
	}
}

void Timer::reInit()
{
	_beginTime = getAbsolute();
	_pauseBeginTime = _beginTime;
}

void Timer::addChild(Timer* timer)
{
	if (timer != nullptr)
	{
		timer->_parent = this;
		_children.push_back(timer);
	}
}

void Timer::removeChild(Timer* timer)
{
	if (timer != nullptr && timer->_parent == this)
	{
		timer->_parent = nullptr;
		size_t i = 0;
		while (i < _children.size())
		{
			if (_children[i] == timer)
			{
				_children.erase(_children.begin() + i);
			}
			else
			{
				i++;
			}
		}
	}
}

UTime Timer::getAbsolute()
{
	if (_parent != nullptr)
	{
		return _parent->get();
	}
	else
	{
#if (defined USE_TIME64)
		return SDL_GetTicks64();
#else
		return SDL_GetTicks();
#endif // (defined USE_TIME64)

	}
}

EngineBase::EngineBase()
{
	windowHeight = 0;
	windowWidth = 0;
	realMousePosX = -1;
	realMousePosY = -1;
}

EngineBase::~EngineBase()
{
	destroyEngineBase();
}

void EngineBase::clearCursor()
{
	cursorImage.image.resize(0);
	cursorImage.interval = 0;
}

void EngineBase::updateCursor()
{
    
}

void EngineBase::drawCursor()
{
	int frameIndex = -1;
	if (cursorImage.interval <= 0)
	{
		if (cursorImage.image.size() >= 1)
		{
			frameIndex = 0;
		}
	}
	else
	{
		if (cursorImage.image.size() >= 1)
		{
			frameIndex = ((int)(getTime() / cursorImage.interval)) % cursorImage.image.size();
		}	
	}
	if (CursorImageIndex == frameIndex && hardwareCursor)
	{
		return;
	}
	if (frameIndex >= 0 && frameIndex < (int)cursorImage.image.size())
	{
		if (hardwareCursor)
		{
#if  (defined _WIN32) || (defined __MACOSX__)
            SDL_SetCursor(cursorImage.image[frameIndex].frame.get());
#endif // (defined _WIN32) || (defined TARGET_OS_MAC)
		}
		else if (!softwareCursorHidden)
		{
            auto drawMouseX = realMousePosX;
            auto drawMouseY = realMousePosY;
#ifdef __APPLE__
            int w, h, rw, rh;
            SDL_GetWindowSize(window, &w, &h);
            SDL_Metal_GetDrawableSize(window, &rw, &rh);
            drawMouseX *= rw / w;
            drawMouseY *= rh / h;
			Rect dst;
			dst.x = drawMouseX - cursorImage.image[frameIndex].xOffset;
			dst.y = drawMouseY - cursorImage.image[frameIndex].yOffset;
			getImageSize(cursorImage.image[frameIndex].softwareFrame, dst.w, dst.h);
			dst.w *= rw / w;
			dst.h *= rh / h;
			drawImage(cursorImage.image[frameIndex].softwareFrame, nullptr, &dst);
#else
			drawImage(cursorImage.image[frameIndex].softwareFrame, drawMouseX - cursorImage.image[frameIndex].xOffset, drawMouseY - cursorImage.image[frameIndex].yOffset);
#endif
			
		}
		CursorImageIndex = frameIndex;
	}
	
}

void EngineBase::setCursor(CursorImage * mouse)
{
	if (mouse == nullptr)
	{
		return;
	}
	clearCursor();
	cursorImage.image.resize(mouse->image.size());
	for (size_t i = 0; i < cursorImage.image.size(); i++)
	{
		cursorImage.image[i].xOffset = mouse->image[i].xOffset;
		cursorImage.image[i].yOffset = mouse->image[i].yOffset;
		cursorImage.image[i].frame = mouse->image[i].frame;	
		cursorImage.image[i].softwareFrame = mouse->image[i].softwareFrame;
	}
	cursorImage.interval = mouse->interval;

	SDL_SetCursor(cursorImage.image[0].frame.get());

}

void EngineBase::showCursor()
{
	if (hardwareCursor)
	{
		SDL_ShowCursor(1);
	}
	softwareCursorHidden = false;
}

void EngineBase::hideCursor()
{
	SDL_ShowCursor(0);
	softwareCursorHidden = true;
}

void EngineBase::drawImage(_shared_image image, SDL_Rect * src, SDL_Rect * dst)
{
	if (image == nullptr)
	{
		return;
	}
	SDL_RenderCopy(renderer, image.get(), src, dst);
}

void EngineBase::drawImage(_shared_image image, SDL_Rect * rect)
{
	if (image == nullptr)
	{
		return;
	}
	SDL_RenderCopy(renderer, image.get(), nullptr, rect);
}

bool EngineBase::pointInImage(_shared_image image, int x, int y)
{
	SDL_Texture * from = image.get();
	int w = 0;
	int h = 0;
	getImageSize(image, w, h);
	if (w <= 0 || h <= 0 || x < 0 || y < 0 || x >= w || y >= h)
	{
		return false;
	}

	_shared_image ts = make_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h));
	auto backendTexture = SDL_GetRenderTarget(renderer);
	SetRenderTarget(renderer, ts.get());

	SDL_BlendMode bm;
	SDL_GetTextureBlendMode(from, &bm);
	SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, from, nullptr, nullptr);
	SDL_SetTextureBlendMode(from, bm);

	std::unique_ptr<char[]> st(new char[w * h * 4]);
	int pitch = 4 * w;
	if (st.get() == nullptr)
	{
		SetRenderTarget(renderer, backendTexture);
		return false;
	}
	if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, st.get(), pitch) != 0)
	{
		SetRenderTarget(renderer, backendTexture);
		return false;
	}
	SDL_Surface * sur = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	memcpy(sur->pixels, st.get(), pitch * h);
	SetRenderTarget(renderer, backendTexture);
	SDL_FreeSurface(sur);
	bool result = false;
	if (st != nullptr)
	{
		if (*(st.get() + y * pitch + x * 4 + 3) != 0)
		{
			result = true;
		}
	}

	return result;
}

void EngineBase::initTime()
{
	timer.setParent(nullptr);
	timer.reInit();
}


UTime EngineBase::getTime()
{
	return timer.get();
	/*if (time.paused)
	{
		return time.pauseBeginTime - time.beginTime;
	}
	else
	{
		return SDL_GetTicks() - time.beginTime;
	}*/
}

//void EngineBase::setTimePaused(bool paused)
//{
//	if (paused == time.paused)
//	{
//		return;
//	}
//	if (paused)
//	{
//		time.paused = true;
//		time.pauseBeginTime = SDL_GetTicks();
//	}
//	else
//	{
//		time.paused = false;
//		time.beginTime += SDL_GetTicks() - time.pauseBeginTime;
//	}
//}
//
//unsigned int EngineBase::initTime(Timer * t)
//{
//	unsigned int now = 0;
//	t->beginTime = getTime();
//	t->paused = false;
//	return t->beginTime;
//}
//
//unsigned int EngineBase::getTime(Timer * t)
//{
//	if (t->paused)
//	{
//		return t->pauseBeginTime - t->beginTime;
//	}
//	else
//	{
//		return getTime() - t->beginTime;
//	}
//}
//
//void EngineBase::setTime(Timer * t, unsigned int time)
//{
//	unsigned int tm = getTime(t);
//	if (tm > time)
//	{
//		t->beginTime += tm - time;
//	}
//	else
//	{
//		t->beginTime -= time - tm;
//	}
//}
//
//void EngineBase::setTimePaused(Timer * t, bool paused)
//{
//	if (paused == t->paused)
//	{
//		return;
//	}
//	if (paused)
//	{
//		t->paused = true;
//		t->pauseBeginTime = getTime();
//	}
//	else
//	{
//		t->paused = false;
//		t->beginTime += getTime() - t->pauseBeginTime;
//	}
//}

void EngineBase::delay(unsigned int t)
{
	SDL_Delay(t);
}

int EngineBase::getFPS()
{
	return FPS;
}

_shared_image EngineBase::createNewImageFromImage(_shared_image image)
{
	if (image == nullptr)
	{
		return nullptr;
	}
	SDL_Texture* from = image.get();
	int w, h; 
	getImageSize(image, w, h);
	if ((w <= 0) || (h <= 0))
	{
		return nullptr;
	}

	auto ts = make_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h));
	auto backendTexture = SDL_GetRenderTarget(renderer);
	SetRenderTarget(renderer, ts.get());
	SDL_BlendMode bm;
	SDL_GetTextureBlendMode(from, &bm);
	SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, from, nullptr, nullptr);
	SDL_SetTextureBlendMode(from, bm);	

	auto to = make_safe_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h));
	auto pixels = std::make_unique<char[]>(w * h * 4);
	int pitch = w * 4;
	SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.get(), pitch);
	SDL_UpdateTexture(to.get(), nullptr, pixels.get(), pitch);
	SetRenderTarget(renderer, backendTexture);

	SDL_SetTextureBlendMode(to.get(), SDL_BLENDMODE_BLEND);
	return to;
}

_shared_image EngineBase::loadImageFromMem(std::unique_ptr<char[]>& data, int size)
{
	if (data == nullptr || size <= 0)
	{
		return nullptr;
	}
    auto img = IMG_LoadTexture_RW(renderer, SDL_RWFromMem(data.get(), size), 1);
    //auto img = IMG_LoadTextureTyped_RW(renderer, SDL_RWFromMem(data.get(), size), 1, "ßPNG");
	_shared_image ret = make_safe_shared_image(img);
	return ret;
}

_shared_image EngineBase::loadImageFromFile(const std::string & fileName)
{
	std::unique_ptr<char[]> data;
	int size;
	if (!File::readFile(fileName, data, size))
	{
		GameLog::write("Image File Readed Error\n");
		return nullptr;
	}
	if (data == nullptr || size <= 0)
	{
		GameLog::write("Image File Readed Error\n");
		return nullptr;
	}
	auto result = loadImageFromMem(data, size);
	//delete[] data;
	return result;
}

int EngineBase::saveImageToFile(_shared_image image, int w, int h, const std::string & fileName)
{
	if (image == nullptr)
	{
		return -1;
	}
	std::unique_ptr<char[]> data = nullptr;
	int len = saveImageToMem(image, w, h, data);
	if (len > 0 && data != nullptr)
	{
		File::writeFile(fileName, data, len);
		return len;
	}
	else
	{
		return -1;
	}
}

int EngineBase::saveImageToFile(_shared_image image, const std::string & fileName)
{
	if (image == nullptr)
	{
		return -1;
	}
	int w, h;
	getImageSize(image, w, h);
	return saveImageToFile(image, w, h, fileName);
}

int EngineBase::saveImageToMem(_shared_image image, int w, int h, std::unique_ptr<char[]>& data)
{
	if (image == nullptr || w <= 0 || h <= 0)
	{
		return -1;
	}
	if (w <= 0 || h <= 0 || image == nullptr)
	{
		return -1;
	}

	SDL_Texture * from = image.get();
	SDL_Texture* ts = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
	SDL_Texture* tt = SDL_GetRenderTarget(renderer);
	SetRenderTarget(renderer, ts);

	SDL_BlendMode bm;
	SDL_GetTextureBlendMode(from, &bm);
	SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, from, nullptr, nullptr);
	SDL_SetTextureBlendMode(from, bm);

	std::unique_ptr<char[]> st(new char[w * h * 4]);
	int pitch = 4 * w;
	if (st == nullptr)
	{
		GameLog::write("allocing memory error\n");
		SetRenderTarget(renderer, tt);
		//freeImage(ts);
		return -1;
	}
	if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, st.get(), pitch) != 0)
	{
		GameLog::write("reading pixels error\n");
		SetRenderTarget(renderer, tt);
		//freeImage(ts);
		/*delete[] st;*/
		return -1;
	}
	SDL_Surface * sur = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	memcpy(sur->pixels, st.get(), pitch * h);

	SetRenderTarget(renderer, tt);
	//freeImage(ts);
	//delete[] st;

	int rwSize = w * h * 4 + 122;
	std::unique_ptr<char[]> rwData(new char[rwSize]);
	
	SDL_RWops * bmp = SDL_RWFromMem(rwData.get(), rwSize);
	bmp->seek(bmp, 0, RW_SEEK_SET);
	if (bmp == nullptr)
	{
		SDL_FreeRW(bmp);
		SDL_FreeSurface(sur);
		return -1;
	}
	SDL_SaveBMP_RW(sur, bmp, 0);

	int size = (int)bmp->seek(bmp, 0, RW_SEEK_END);
	if (size >= rwSize)
	{
		data = std::unique_ptr<char[]>(new char[rwSize]);
		bmp->seek(bmp, 0, 0);
		bmp->read(bmp, data.get(), rwSize, 1);
		SDL_FreeSurface(sur);
		SDL_FreeRW(bmp);
		bmp = nullptr;
		size = rwSize;
	}
	return size;

}

int EngineBase::saveImageToMem(_shared_image image, std::unique_ptr<char[]>& data)
{
	if (image == nullptr)
	{
		return -1;
	}
	int w, h;
	getImageSize(image, w, h);
	return saveImageToMem(image, w, h, data);
}

_shared_surface EngineBase::loadSurfaceFromMem(std::unique_ptr<char[]>& data, int size)
{
	if (data == nullptr || size <= 0)
	{
		return nullptr;
	}
	_shared_surface image = make_shared_surface(IMG_Load_RW(SDL_RWFromMem(data.get(), size), 1));
	return image;
}

_shared_cursor EngineBase::loadCursorFromMem(std::unique_ptr<char[]>& data, int size, int x, int y)
{
	auto s = loadSurfaceFromMem(data, size);
	auto cursor = make_shared_cursor(SDL_CreateColorCursor(s.get(), x, y));
	return cursor;
}

void EngineBase::drawImage(_shared_image image, int x, int y)
{
	if (image == nullptr)
	{
		return;
	}
	int w, h;
	SDL_QueryTexture(image.get(), nullptr, nullptr, &w, &h);
	SDL_Rect s, d;
	s.x = 0;
	s.y = 0;
	s.w = w;
	s.h = h;
	d.x = x;
	d.y = y;
	d.w = w;
	d.h = h;
	SDL_RenderCopy(renderer, image.get(), nullptr, &d);
}

void EngineBase::drawImage(_shared_image image, Rect * rect)
{
	drawImage(image, nullptr, rect);
}

void EngineBase::drawImage(_shared_image image, Rect * src, Rect * dst)
{
	if (image == nullptr)
	{
		return;
	}
	SDL_Rect s;
	SDL_Rect d;
	SDL_Rect * ps = nullptr;
	SDL_Rect * pd = nullptr;
	if (src != nullptr)
	{
		s.x = src->x;
		s.y = src->y;
		s.w = src->w;
		s.h = src->h;
		ps = &s;
	}
	if (dst != nullptr)
	{
		d.x = dst->x;
		d.y = dst->y;
		d.w = dst->w;
		d.h = dst->h;
		pd = &d;
	}
	SDL_RenderCopy(renderer, image.get(), ps, pd);
}

void EngineBase::drawImageEx(_shared_image image, Rect* src, Rect* dst, double angle, Point* center)
{
	if (image == nullptr)
	{
		return;
	}
	SDL_Rect s;
	SDL_Rect d;
	SDL_Rect* ps = nullptr;
	SDL_Rect* pd = nullptr;
	if (src != nullptr)
	{
		s.x = src->x;
		s.y = src->y;
		s.w = src->w;
		s.h = src->h;
		ps = &s;
	}
	if (dst != nullptr)
	{
		d.x = dst->x;
		d.y = dst->y;
		d.w = dst->w;
		d.h = dst->h;
		pd = &d;
	}
	SDL_Point p;
	SDL_Point* pp = nullptr;
	if (center != nullptr)
	{
		p.x = center->x;
		p.y = center->y;
		pp = &p;
	}

	SDL_RenderCopyEx(renderer, image.get(), ps, pd, angle, pp, SDL_FLIP_NONE);
}

//void EngineBase::freeImage(Image_t* image)
//{
//	if (image != nullptr)
//	{
//		SDL_DestroyTexture(image);
//		image = nullptr;
//	}
//}

_shared_image EngineBase::createMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a, bool safe)
{
	SDL_Texture * tt = SDL_GetRenderTarget(renderer);
	auto t = make_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 1, 1));
	if (t.get() == nullptr)
	{
		return nullptr;
	}
	_shared_image t2 = nullptr;
	if (safe)
	{
		t2 = make_safe_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1));
	}
	else
	{
		t2 = make_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1));
	}
	
	if (t2.get() == nullptr)
	{
		return nullptr;
	}
	SetRenderTarget(renderer, t.get());
	SDL_SetRenderDrawColor(renderer, r, g, b, 255);
	SDL_RenderClear(renderer);
	void * pixels = getMem(4);
	int pitch = 4;
	SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels, pitch);
	SDL_UpdateTexture(t2.get(), nullptr, pixels, pitch);
	SetRenderTarget(renderer, tt);
	freeMem(pixels);
	SDL_SetTextureBlendMode(t2.get(), SDL_BLENDMODE_BLEND);
	setImageAlpha(t2, a);
	//freeImage(t);
	return t2;
}

_shared_image EngineBase::createLumMask()
{
	SDL_Surface * s = SDL_CreateRGBSurfaceWithFormat(0, LUM_MASK_WIDTH, LUM_MASK_HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
	auto c = new Uint32[LUM_MASK_HEIGHT][LUM_MASK_WIDTH];
	for (int i = 0; i < LUM_MASK_HEIGHT; i++)
	{
		for (int j = 0; j < LUM_MASK_WIDTH; j++)
		{
			double distance = std::abs(hypot(double(i - LUM_MASK_HEIGHT / 2) / (LUM_MASK_HEIGHT / 2), double(j - LUM_MASK_WIDTH / 2) / (LUM_MASK_WIDTH / 2)));
			if (distance >= 0.5)
			{
				c[i][j] = 0;
			}
			else
			{
				unsigned char a = (unsigned char)((0.5 - distance) * LUM_MASK_MAX_ALPHA);
				c[i][j] = (a << 24) | (0xFFFFFF);
			}
		}
	}
	memcpy(s->pixels, c, LUM_MASK_HEIGHT * LUM_MASK_WIDTH * 4);
	delete[] c;
	auto t = make_safe_shared_image(SDL_CreateTextureFromSurface(renderer, s));
	SDL_FreeSurface(s);
	SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_ADD);
	return t;
}

void EngineBase::setImageAlpha(_shared_image image, unsigned char a)
{
	SDL_SetTextureBlendMode(image.get(), SDL_BLENDMODE_BLEND);
	SDL_SetTextureAlphaMod(image.get(), a);	
}

void EngineBase::setImageColorMode(_shared_image image, unsigned char r, unsigned char g, unsigned char b)
{
	SDL_SetTextureColorMod(image.get(), r, g, b);
}

void EngineBase::drawImageWithColor(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
	if (image == nullptr)
	{
		return;
	}
	SDL_SetTextureColorMod(image.get(), r, g, b);
	drawImage(image, x, y);
	SDL_SetTextureColorMod(image.get(), 255, 255, 255);
}

int EngineBase::getImageSize(_shared_image image, int& w, int& h)
{
	if (image == nullptr)
	{
		return -1;
	}
	return SDL_QueryTexture(image.get(), nullptr, nullptr, &w, &h);
}

bool EngineBase::beginDrawTalk(int w, int h)
{
	if (w <= 0 || h <= 0)
	{
		return false;
	}
	originalTex = SDL_GetRenderTarget(renderer);
	auto t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetTextureAlphaMod(t, 255);
	SetRenderTarget(renderer, t);
	SDL_Surface * ts = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
	Uint32 color = 0xFFFFFF;
	memcpy(ts->pixels, &color, 4);
	SDL_Texture * t2 = SDL_CreateTextureFromSurface(renderer, ts);
	SDL_FreeSurface(ts);
	SDL_SetTextureBlendMode(t2, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, t2, nullptr, nullptr);
	SDL_DestroyTexture(t2);
	return true;
}

_shared_image EngineBase::endDrawTalk()
{
	auto t = make_shared_image(SDL_GetRenderTarget(renderer));
	SetRenderTarget(renderer, originalTex);
	return createNewImageFromImage(t);
	//SDL_DestroyTexture(t);
}

_shared_image EngineBase::loadSaveShotFromPixels(int w, int h, std::unique_ptr<char[]>& data, int size)
{
	if (w <= 0 || h <= 0 || data == nullptr || size <= 0 || w * h * SaveBMPPixelBytes > size)
	{
		GameLog::write("save shot null\n");
		return nullptr;
	}
	auto pitch = w * SaveBMPPixelBytes;
	auto result = make_safe_shared_image(SDL_CreateTexture(renderer, SaveBMPFormat, SDL_TEXTUREACCESS_STATIC, w, h));
	SDL_UpdateTexture(result.get(), nullptr, data.get(), pitch);
	SDL_SetTextureBlendMode(result.get(), SDL_BLENDMODE_BLEND);
	return result;
	//_shared_surface sur = make_shared_surface(SDL_CreateRGBSurfaceWithFormat(0, w, h, SaveBMPPixelBytes * 8, SaveBMPFormat));
	//if (sur == nullptr)
	//{
	//	return nullptr;
	//}
	/*if (w % SaveBMPPixelBytes == 0)
	{
		memcpy(sur->pixels, data.get(), w * SaveBMPPixelBytes * h);
	}
	else
	{
		for (int i = 0; i < h; i++)
		{
			if (i < w)
			{
				memcpy(((char *)sur->pixels) + w * i * SaveBMPPixelBytes, data.get() + i * w * SaveBMPPixelBytes + w * SaveBMPPixelBytes - i * SaveBMPPixelBytes, i * SaveBMPPixelBytes);
				memcpy(((char *)sur->pixels) + w * i * SaveBMPPixelBytes + i * SaveBMPPixelBytes, data.get() + i * w * SaveBMPPixelBytes, w * SaveBMPPixelBytes - i * SaveBMPPixelBytes);
			}
			else
			{
				memcpy(((char *)sur->pixels) + w * i * SaveBMPPixelBytes, data.get() + i * w * SaveBMPPixelBytes, w * SaveBMPPixelBytes);
			}
		}
	}*/
	/*return make_shared_image(SDL_CreateTextureFromSurface(renderer, sur.get()));*/
	//SDL_FreeSurface(sur);
}

bool EngineBase::beginSaveScreen()
{
	originalTex = SDL_GetRenderTarget(renderer);
	auto t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width, height);
	if (t == nullptr)
	{
		return false;
	}
	SetRenderTarget(renderer, t);
	return true;
}

_shared_image EngineBase::endSaveScreen()
{
	auto t = make_shared_image(SDL_GetRenderTarget(renderer));
	SetRenderTarget(renderer, originalTex);
	return createNewImageFromImage(t);
}

int EngineBase::saveImageToPixels(_shared_image image, int w, int h, std::unique_ptr<char[]>& s)
{
	if (w <= 0 || h <= 0 || image == nullptr)
	{
		return -1;
	}

	auto from = image;
	auto tempImage = make_shared_image(SDL_CreateTexture(renderer, SaveBMPFormat, SDL_TEXTUREACCESS_TARGET, w, h));
	SDL_Texture* tt = SDL_GetRenderTarget(renderer);
	SetRenderTarget(renderer, tempImage.get());	

	SDL_BlendMode bm;
	SDL_GetTextureBlendMode(from.get(), &bm);
	SDL_SetTextureBlendMode(from.get(), SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, from.get(), nullptr, nullptr);
	SDL_SetTextureBlendMode(from.get(), bm);

	auto buffer = std::make_unique<char[]>(w * h * SaveBMPPixelBytes);
	int pitch = SaveBMPPixelBytes * w;

	if (SDL_RenderReadPixels(renderer, nullptr, SaveBMPFormat, buffer.get(), pitch) != 0)
	{
		return -1;
	}
	s = std::move(buffer);
	
	SetRenderTarget(renderer, tt);

	//freeImage(ts);

	return w * h * SaveBMPPixelBytes;
}

_shared_image EngineBase::createRaindrop()
{
	const int w = 2;
	const int h = 115;

	SDL_Surface * s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (s == nullptr)
	{
		return nullptr;
	}
	unsigned int col = 0xFFFFFFFF;
	//SDL_LockSurface(s);
	memset(s->pixels, 0, w * h * 4);
	for (int i = 0; i < h; i++)
	{
		col = 0xFFFFFF | (((unsigned int)(i * 1.3)) << 24);
		for (int j = 0; j < w; j++)
		{
			memcpy(((char *)s->pixels) + j * 4 + i * w * 4, &col, 4);			
		}
	}
	//SDL_UnlockSurface(s);
	auto t = make_safe_shared_image(SDL_CreateTextureFromSurface(renderer, s));
	SDL_FreeSurface(s);
	SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_BLEND);
	return t;
}

_shared_image EngineBase::createSnowflake()
{
	SDL_Surface * s = SDL_CreateRGBSurfaceWithFormat(0, 3, 3, 32, SDL_PIXELFORMAT_ARGB8888);
	if (s == nullptr)
	{
		return nullptr;
	}
	unsigned int col = 0xFFFFFFFF;
	//SDL_LockSurface(s);
	memset(s->pixels, 0, 3 * 3 * 4);
	memcpy(((char *)s->pixels) + 4, &col, 4);
	memcpy(((char *)s->pixels) + 12, &col, 4);
	memcpy(((char *)s->pixels) + 16, &col, 4);
	memcpy(((char *)s->pixels) + 20, &col, 4);
	memcpy(((char *)s->pixels) + 28, &col, 4);
	//SDL_UnlockSurface(s);
	auto t = make_safe_shared_image(SDL_CreateTextureFromSurface(renderer, s));
	SDL_FreeSurface(s);
	setImageAlpha(t, 0xB0);
	return t;
}

void EngineBase::loadLogo()
{
	//if (logo != nullptr)
	//{
	//	freeImage(logo);
	//	logo = nullptr;
	//}
	std::string logoFileName = "config\\logo.png";
#ifdef USE_LOGO_RESOURCE
	HRSRC hRsrc = FindResource(nullptr, TEXT("PNGIMAGE"), RT_RCDATA);
	if (hRsrc == nullptr)
	{
		logo = loadImageFromFile(logoFileName);
		GameLog::write("Logo Loaded From File\n");
		return;
	}
	GameLog::write("Logo Loaded From Resource\n");

	unsigned int size = SizeofResource(nullptr, hRsrc);
	if (size == 0)
	{
		logo = loadImageFromFile(logoFileName);
		return;
	}
	HGLOBAL hGlobal = LoadResource(nullptr, hRsrc);
	if (hGlobal == nullptr)
	{
		logo = loadImageFromFile(logoFileName);
		return;
	}
	LPVOID pBuffer = LockResource(hGlobal);
	if (pBuffer == nullptr)
	{
		logo = loadImageFromFile(logoFileName);
		return;
	}
	std::unique_ptr<char[]> data = std::unique_ptr<char[]>((char*)pBuffer);
	logo = loadImageFromMem(data, size);
	UnlockResource(hGlobal);
	FreeResource(hGlobal);
#else
	logo = loadImageFromFile(logoFileName);
#endif // USE_LOGO_RESOURCE
}

void EngineBase::fadeInLogo()
{
	if (logo == nullptr)
	{
		return;
	}
	int w, h;
	if (getImageSize(logo, w, h) == 0)
	{
		unsigned char r, g, b;
		r = (clLogoBG & 0xFF0000) >> 16;
		g = (clLogoBG & 0xFF00) >> 8;
		b = clLogoBG & 0xFF;
		setScreenMask(r, g, b, 255);
		Timer t(&timer);
		t.reInit();
		auto now = t.get();
		while (now < 1000)
		{
			frameBegin();
			drawScreenMask();
			unsigned char a = (unsigned char)(((double)now) / (double)1000 * (double)255);
			setImageAlpha(logo, a);
			drawImage(logo, (width - w) / 2, (height - h) / 2);
			frameEnd();
			now = t.get();
		}


		frameBegin();
		drawScreenMask();
		setImageAlpha(logo, (unsigned char)255);
		drawImage(logo, (width - w) / 2, (height - h) / 2);
		frameEnd();


		frameBegin();
		drawScreenMask();
		setImageAlpha(logo, 255);
		drawImage(logo, (width - w) / 2, (height - h) / 2);
		frameEnd();
	}
	return;
}

void EngineBase::fadeOutLogo()
{
	if (logo == nullptr)
	{
		return;
	}
	int w, h;
	if (getImageSize(logo, w, h) == 0)
	{
		Timer t(&timer);
		t.reInit();
		auto now = t.get()
;		while (now < 1000)
		{
			frameBegin();
			drawScreenMask();
			unsigned char a = (unsigned char)((1000 - now) / (double)1000 * (double)255);
			setImageAlpha(logo, a);
			drawImage(logo, (width - w) / 2, (height - h) / 2);
			frameEnd();
			now = t.get();
		}

		frameBegin();
		drawScreenMask();
		frameEnd();
	}
}

void EngineBase::drawImageWithMask(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == nullptr)
	{
		return;
	}
	_shared_image mask = createMask(r, g, b, a);
	drawImageWithMask(image, x, y, mask);
	//freeImage(mask);
}

void EngineBase::drawImageWithMask(_shared_image image, int x, int y, _shared_image mask)
{
	if (image == nullptr || mask == nullptr)
	{
		return;
	}
	int w, h;
	SDL_QueryTexture(image.get(), nullptr, nullptr, &w, &h);
	SDL_Texture * tt = SDL_GetRenderTarget(renderer);
	drawImage(image, x, y);
	SDL_Rect sdlrect;
	sdlrect.x = x;
	sdlrect.y = y;
	sdlrect.w = w;
	sdlrect.h = h;
	SDL_RenderCopy(renderer, mask.get(), nullptr, &sdlrect);
}

void EngineBase::drawImageWithMask(_shared_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == nullptr)
	{
		return;
	}
	_shared_image mask = createMask(r, g, b, a);
	drawImageWithMask(image, src, dst, mask);
	//freeImage(mask);
}

void EngineBase::drawImageWithMask(_shared_image image, Rect * src, Rect * dst, _shared_image mask)
{
	if (image == nullptr || mask == nullptr)
	{
		return;
	}
	SDL_Rect d;
	SDL_Rect * pd = nullptr;
	if (dst != nullptr)
	{
		d.x = dst->x;
		d.y = dst->y;
		d.w = dst->w;
		d.h = dst->h;
		pd = &d;
	}
	drawImage(image, src, dst);
	SDL_RenderCopy(renderer, mask.get(), nullptr, pd);
}

void EngineBase::drawImageWithMaskEx(_shared_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == nullptr)
	{
		return;
	}
	_shared_image mask = createMask(r, g, b, a);
	drawImageWithMaskEx(image, x, y, mask);
	//freeImage(mask);
}

void EngineBase::drawImageWithMaskEx(_shared_image image, int x, int y, _shared_image mask)
{
	if (image == nullptr || mask == nullptr)
	{
		return;
	}
	int w, h;
	getImageSize(image, w, h);
	if (w <= 0 || h <= 0)
	{
		return;
	}
	Rect dst;
	dst.x = x;
	dst.w = w;
	dst.y = y;
	dst.h = h;
	drawImageWithMaskEx(image, nullptr, &dst, mask);
}

void EngineBase::drawImageWithMaskEx(_shared_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == nullptr)
	{
		return;
	}
	_shared_image mask = createMask(r, g, b, a);
	drawImageWithMaskEx(image, src, dst, mask);
	//freeImage(mask);
}

void EngineBase::drawImageWithMaskEx(_shared_image image, Rect * src, Rect * dst, _shared_image mask)
{
	if (image == nullptr || mask == nullptr)
	{
		return;
	}
	int w, h;
	SDL_QueryTexture(image.get(), nullptr, nullptr, &w, &h);
	auto t = make_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h));
	SDL_Texture * tt = SDL_GetRenderTarget(renderer);
	SetRenderTarget(renderer, t.get());
	SDL_SetTextureBlendMode(image.get(), SDL_BLENDMODE_NONE);
	drawMask(image);
	SDL_SetTextureBlendMode(image.get(), SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(mask.get(), SDL_BLENDMODE_ADD);
	drawMask(mask);
	SetRenderTarget(renderer, tt);
	SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_BLEND);
	drawImage(t, src, dst);
	//freeImage(t);
}

void EngineBase::setScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (screenMask == nullptr)
	{
		return;
	}
	*(unsigned int *)(screenMask)->pixels = 0xFF << 24 | r << 16 | g << 8| b;
	SDL_SetSurfaceAlphaMod(screenMask.get(), a);
}

void EngineBase::drawScreenMask()
{
	if (screenMask == nullptr)
	{
		return;
	}
	auto t = make_shared_image(SDL_CreateTextureFromSurface(renderer, screenMask.get()));
	SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_BLEND);
	drawMask(t);
	//freeImage(t);
}

void EngineBase::drawMask(_shared_image mask)
{
	drawMask(mask, nullptr);
}

void EngineBase::drawMask(_shared_image mask, Rect* dst)
{
	if (mask == nullptr)
	{
		return;
	}
	SDL_Rect d;
	SDL_Rect * pd = nullptr;
	if (dst != nullptr)
	{
		d.x = dst->x;
		d.y = dst->y;
		d.w = dst->w;
		d.h = dst->h;
		pd = &d;
	}
	SDL_RenderCopy(renderer, mask.get(), nullptr, pd);
}

void EngineBase::handleEvent()
{
	clearEventList();
	SDL_Event e;
	timer.setPaused(true);
	while (SDL_PollEvent(&e))
	{
		switch (e.type)
		{
			case SDL_QUIT:
			{
				eventList.event.push(AEvent(ET_QUIT , 0, 0, 0));
				break;
			}
			case SDL_KEYDOWN: case SDL_KEYUP:
			{	
				eventList.event.push(AEvent((EventType)e.type , e.key.keysym.scancode, 0, 0));
				break;
			}
			case SDL_MOUSEMOTION:
			{
                realMousePosX = e.motion.x;
                realMousePosY = e.motion.y;
#ifndef __MOBILE__
				int tempX = -1, tempY = -1;
				calculateCursor(e.motion.x, e.motion.y, &tempX, &tempY);

				if (tempX >= 0 && tempY >= 0)
				{
					mouseX = tempX;
					mouseY = tempY;
					eventList.event.push(AEvent(EventType::ET_MOUSEMOTION, (int)TOUCH_MOUSEID, tempX, tempY));
				}
#endif // (!defined __MOBILE__)
				break;
			}
			case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
			{
                realMousePosX = e.motion.x;
                realMousePosY = e.motion.y;
#ifndef __MOBILE__
				//鼠标点击时增加一个鼠标移动事件
				int tempX = -1, tempY = -1;
				calculateCursor(e.motion.x, e.motion.y, &tempX, &tempY);
				if (tempX >= 0 && tempY >= 0)
				{
					mouseX = tempX;
					mouseY = tempY;
					eventList.event.push(AEvent((EventType)EventType::ET_MOUSEMOTION, (int)TOUCH_MOUSEID, tempX, tempY));
				}
				eventList.event.push(AEvent((EventType)e.type, e.button.button, tempX, tempY));
#endif // (!defined __MOBILE__)
				break;
			}
			case SDL_FINGERDOWN: case SDL_FINGERUP: 
			{
				int tempWidth = 0;
				int tempHeight = 0;
				SDL_GetWindowSize(window, &tempWidth, &tempHeight);
				int tempX = -1;
				int tempY = -1;
				calculateCursor((int)round(e.tfinger.x * tempWidth), (int)round(e.tfinger.y * tempHeight), &tempX, &tempY);
				if (tempX >= 0 && tempY >= 0)
				{
					eventList.event.push(AEvent(ET_FINGERMOTION, (int)e.tfinger.fingerId, tempX, tempY));
				}
				eventList.event.push(AEvent((EventType)e.type , (int)e.tfinger.fingerId, tempX, tempY));
				break;
			}
			case SDL_FINGERMOTION:
			{
				int tempWidth = 0;
				int tempHeight = 0;
				SDL_GetWindowSize(window, &tempWidth, &tempHeight);
				int tempX = -1;
				int tempY = -1;
				calculateCursor((int)round(e.tfinger.x * tempWidth), (int)round(e.tfinger.y * tempHeight), &tempX, &tempY);
				if (tempX >= 0 && tempY >= 0)
				{
					eventList.event.push(AEvent((EventType)e.type , (int)e.tfinger.fingerId, tempX, tempY));
				}
				break;
			}
			default:
			{
				break;
			}			
		}
	}
#ifndef __MOBILE__
	int tempX = -1, tempY = -1, mX, mY;
	SDL_GetMouseState(&mX, &mY);
	calculateCursor(mX, mY, &tempX, &tempY);
	if (tempX >= 0 && tempY >= 0)
	{
		mouseX = tempX;
		mouseY = tempY;
		eventList.event.push(AEvent(EventType::ET_MOUSEMOTION, (int)TOUCH_MOUSEID, tempX, tempY));
	}
#endif // !__MOBILE__

	timer.setPaused(false);
}

void EngineBase::copyEvent(AEvent& s, AEvent& d)
{
	d.eventType = s.eventType;
	d.eventData = s.eventData;
	d.eventX = s.eventX;
	d.eventY = s.eventY;
}

void EngineBase::clearEventList()
{
	while (!eventList.event.empty()) { eventList.event.pop(); }
}

int EngineBase::getEventCount()
{
	return (int)eventList.event.size();
}

int EngineBase::getEvent(AEvent& event)
{
	if (eventList.event.size() == 0)
	{
		return 0;
	}
	copyEvent(eventList.event.front(), event);
	eventList.event.pop();
	return (int)eventList.event.size() + 1;
}

void EngineBase::pushEvent(AEvent& event)
{
	eventList.event.push(event);
}

bool EngineBase::getKeyPress(KeyCode key)
{
	return (SDL_GetKeyboardState(nullptr)[key] != 0);
}

bool EngineBase::getMousePress(MouseButtonCode button)
{
	return ((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(button)) != 0);
}

void EngineBase::getMouse(int& x, int& y)
{
	x = mouseX;
	y = mouseY;
}

void EngineBase::resetEvent()
{
	handleEvent();
	clearEventList();
}

void EngineBase::setCursorHardware(bool isHardware)
{
	hardwareCursor = isHardware;
	if (hardwareCursor && !softwareCursorHidden)
	{
		SDL_ShowCursor(1);
	}
	else
	{
		SDL_ShowCursor(0);
	}
}

void EngineBase::setFontName(const std::string & fontName)
{
	font = fontName;
	if (fontData != nullptr)
	{
		SDL_FreeRW(fontData);
		fontData = nullptr;
	}
	fontData = SDL_RWFromFile(fontName.c_str(), "r+");
	if (!fontData)
	{
		GameLog::write("there is no fontData\n");
	}
}

void EngineBase::drawTalk(const std::string& text, int x, int y, int size, unsigned int color)
{
	_shared_image t = createText(text, size, color);
	SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_NONE);
	drawImage(t, x, y);
	//freeImage(t);
}

void EngineBase::drawSolidUnicodeText(const std::wstring & text, int x, int y, int size, unsigned int color)
{
	_shared_image t = createUnicodeText(text, size, color);
	SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_NONE);
	drawImage(t, x, y);
	//freeImage(t);
}

void EngineBase::setFontFromMem(std::unique_ptr<char[]>& data, int size)
{
	if (data == nullptr || size <= 0)
	{
		return;
	}
	if (fontData != nullptr)
	{
		SDL_FreeRW(fontData);
		fontData = nullptr;
	}
	if (fontBuffer != nullptr)
	{
		fontBuffer = nullptr;
	}
	fontData = SDL_RWFromMem(data.get(), size);
	fontBuffer = std::move(data);
}

_shared_image EngineBase::createUnicodeText(const std::wstring& text, int size, unsigned int color)
{

	TTF_Font* _font = nullptr;
	if (!fontData)
	{
		_font = TTF_OpenFont(font.c_str(), size);
	}
	else
	{
		SDL_RWseek(fontData, 0, RW_SEEK_SET);
		_font = TTF_OpenFontRW(fontData, 0, size);
	}
	if (!_font)
	{
		return nullptr;
	}
	SDL_Color c;
	c.b = (color & 0xFF);
	c.g = (color & 0xFF00) >> 8;
	c.r = (color & 0xFF0000) >> 16;
	c.a = (color & 0xFF000000) >> 24;

	auto text_s = TTF_RenderUNICODE_Blended(_font, (Uint16*)text.c_str(), c);
	auto text_t = make_shared_image(SDL_CreateTextureFromSurface(renderer, text_s));

	setImageAlpha(text_t, c.a);

	SDL_FreeSurface(text_s);
	TTF_CloseFont(_font);
	return text_t;

}

_shared_image EngineBase::createText(const std::string& text, int size, unsigned int color, bool safe)
{
	TTF_Font * _font = nullptr;
	if (!fontData)
	{
		_font = TTF_OpenFont(font.c_str(), size);
	}
	else
	{
		SDL_RWseek(fontData, 0, RW_SEEK_SET);
		_font = TTF_OpenFontRW(fontData, 0, size);	
	}	
	if (!_font) 
	{ 
		return nullptr; 
	}

	SDL_Color c;
	c.b = (color & 0xFF);
	c.g = (color & 0xFF00) >> 8;
	c.r = (color & 0xFF0000) >> 16;	
	c.a = (color & 0xFF000000) >> 24;

	auto text_s = TTF_RenderUTF8_Blended(_font, text.c_str(), c);
	_shared_image text_t;
	if (safe)
	{
		text_t = make_safe_shared_image(SDL_CreateTextureFromSurface(renderer, text_s));
	}
	else
	{
		text_t = make_shared_image(SDL_CreateTextureFromSurface(renderer, text_s));
	}

	setImageAlpha(text_t, c.a);

	SDL_FreeSurface(text_s);
	TTF_CloseFont(_font);
	return text_t;
}

void EngineBase::drawUnicodeText(const std::wstring& text, int x, int y, int size, unsigned int color)
{
	_shared_image t = createUnicodeText(text, size, color);
	drawImage(t, x, y);
	//freeImage(t);
}

void EngineBase::drawText(const std::string & text, int x, int y, int size, unsigned int color)
{
	_shared_image t = createText(text, size, color);
	drawImage(t, x, y);	
	//freeImage(t);
}

int EngineBase::enginebaseAppEventHandler(void* userdata, SDL_Event* event)
{
	if (_externalEventHandler != NULL)
	{
		_externalEventHandler(event);
	}

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
		//_mutex.lock();
        if (!isBackGround)
        {
            isBackGround = true;
            tempRenderTarget = SDL_GetRenderTarget(renderer);
            SDL_SetRenderTarget(renderer, nullptr);
        }
		//_mutex.unlock();
		return 0;
	case SDL_APP_DIDENTERBACKGROUND:
		/* This will get called if the user accepted whatever sent your app to the background.
			If the user got a phone call and canceled it, you'll instead get an SDL_APP_DIDENTERFOREGROUND event and restart your loops.
			When you get this, you have 5 seconds to save all your state or the app will be terminated.
			Your app is NOT active at this point.
		*/
        //_mutex.lock();
        if (!isBackGround)
        {
            isBackGround = true;
            tempRenderTarget = SDL_GetRenderTarget(renderer);
            SDL_SetRenderTarget(renderer, nullptr);
        }
        //_mutex.unlock();
		return 0;
	case SDL_APP_WILLENTERFOREGROUND:
		/* This call happens when your app is coming back to the foreground.
			Restore all your state here.
		*/
		//_mutex.lock();
        if (isBackGround)
        {
            SDL_SetRenderTarget(renderer, tempRenderTarget);
            tempRenderTarget = nullptr;
            isBackGround = false;
        }
		//_mutex.unlock();
		return 0;
	case SDL_APP_DIDENTERFOREGROUND:
		/* Restart your loops here.
			Your app is interactive and getting CPU again.
		*/
        //_mutex.lock();
        if (isBackGround)
        {
            SDL_SetRenderTarget(renderer, tempRenderTarget);
            tempRenderTarget = nullptr;
            isBackGround = false;
        }
        //_mutex.unlock();
		return 0;
	default:
		/* No special processing, add it to the event queue */
		return 1;
	}
}

int EngineBase::SetRenderTarget(SDL_Renderer* r, SDL_Texture* t)
{
	if (isBackGround)
	{
		tempRenderTarget = t;
		return 0;
	}
	else
	{
		return SDL_SetRenderTarget(r, t);
	}
}

InitErrorType EngineBase::init(const std::string & windowCaption, int & wWidth, int & wHeight, FullScreenMode fullScreenMode, FullScreenSolutionMode fullScreenSolutionMode, AppEventHandler eventHandler)
{
	width = wWidth;
	height = wHeight;
	if (initSDL(windowCaption, wWidth, wHeight, fullScreenMode, fullScreenSolutionMode) != 0)
	{
		GameLog::write("Init SDL Error!\n");
		return sdlError;
	}
	
    wWidth = width;
    wHeight = height;
    
	_externalEventHandler = eventHandler;
	SDL_SetEventFilter(enginebaseAppEventHandler, nullptr);

#ifdef SHF_USE_AUDIO
	if (initSoundSystem() != 0)
	{
		GameLog::write("Init Sound Error!\n");
		return soundError;
	}
#endif

#ifdef SHF_USE_VIDEO
	if (initVideo() != 0)
	{
		GameLog::write("Init Video Error!\n");
		return videoError;
	}
#endif

	if (lzo_init() != LZO_E_OK)
	{
		GameLog::write("Init miniLZO Error!\n");
		return LZOError;
	}
	SDL_StopTextInput();
	fadeOutLogo();

	resetEvent();

	if (hardwareCursor)
	{
		showCursor();
	}
	return initOK;
}

void EngineBase::destroyEngineBase()
{
	if (lzoMem != nullptr)
	{
		freeMem(lzoMem);
		lzoMem = nullptr;
	}

	//freeImage(logo);

#ifdef SHF_USE_VIDEO
	destroyVideo();
#endif
#ifdef SHF_USE_AUDIO
	destroySoundSystem();
#endif
    SDL_SetEventFilter(nullptr, nullptr);
	destroySDL();
}

void EngineBase::setFullScreen(FullScreenMode mode)
{
	if (mode == _fullScreenMode)
	{
		return;
	}
    _fullScreenMode = mode;
	unsigned int flags = SDL_GetWindowFlags(window);
	if (mode == FullScreenMode::fullScreen)
	{
        if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
            flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
        flags |= SDL_WINDOW_FULLSCREEN;
	}
	else if (mode == FullScreenMode::windowFullScreen)
	{
        if (flags & SDL_WINDOW_FULLSCREEN) {
            flags ^= SDL_WINDOW_FULLSCREEN;
        }
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
    else
    {
        if (flags & SDL_WINDOW_FULLSCREEN) {
            flags ^= SDL_WINDOW_FULLSCREEN;
        }
        if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
            flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
    }
    SDL_SetWindowFullscreen(window, flags);
    if (mode == FullScreenMode::window) {
//        SDL_DisplayMode dm;
//        SDL_GetDisplayMode(0, 0, &dm);

        SDL_SetWindowSize(window, width, height);
//        SDL_SetWindowPosition(window, (dm.w - width) / 2, (dm.h - height) / 2);
    }

	updateState();
    return;
}

void EngineBase::getScreenInfo(int& w, int& h)
{
	SDL_DisplayMode sdl_dm;
	SDL_GetCurrentDisplayMode(0, &sdl_dm);
	w = sdl_dm.w;
	h = sdl_dm.h;
}

void EngineBase::setWindowSize(int w, int h)
{
	if (w < 0 || h < 0)
	{
		return;
	}
	if (w != width || h != height)
	{
		width = w;
		height = h;
		SDL_SetWindowSize(window, w, h);
		SDL_Texture * t = SDL_GetRenderTarget(renderer);
		bool renderRealScreen = (t == realScreen.get());
		auto newScreen = make_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width, height));
		SetRenderTarget(renderer, newScreen.get());
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
		SDL_RenderClear(renderer);
		drawImage(realScreen, (Rect *)nullptr);
		if (!renderRealScreen)
		{
			SetRenderTarget(renderer, t);
		}
		//freeImage(realScreen);
		realScreen = (newScreen);
		updateState();
	}
	else
	{
		if (windowWidth != width || windowHeight != height)
		{
			SDL_SetWindowSize(window, width, height);
			updateState();
		}
	}
}

void EngineBase::countFPS()
{
	if (SDL_GetTicks() - FPSTime > 1000)
	{
		FPS = FPSCounter;
		FPSTime = SDL_GetTicks();
		FPSCounter = 0;
	}
	else
	{
		FPSCounter++;
	}	
}

InitErrorType EngineBase::initSDL(const std::string & windowCaption, int wWidth, int wHeight, FullScreenMode fullScreenMode, FullScreenSolutionMode fullScreenSolutionMode)
{
#ifdef __MOBILE__
    fullScreenMode = FullScreenMode::fullScreen;
    fullScreenSolutionMode = FullScreenSolutionMode::adjust;
#endif
    _fullScreenMode = fullScreenMode;
    _fullScreenSolutionMode = fullScreenSolutionMode;
    
	if (SDL_Init(SDL_INIT_EVERYTHING ^ SDL_INIT_AUDIO))
	{
		GameLog::write("SDL error: %s \n", SDL_GetError());
		return sdlError;
	}

	SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "1");
    
    int screenWidth = wWidth;
    int screenHeight = wHeight;
    getScreenInfo(screenWidth, screenHeight);
    

    if (fullScreenMode != FullScreenMode::window && fullScreenSolutionMode != FullScreenSolutionMode::forceToUseSetting)
    {
        if (fullScreenSolutionMode == FullScreenSolutionMode::original) 
        {
            width = screenWidth;
            height = screenHeight;
        }
        else if (fullScreenSolutionMode == FullScreenSolutionMode::adjust)
        {
            if (((double)screenWidth) / screenHeight > ((double)wWidth) / wHeight)
            {
                wWidth  = (int)round(((double)wHeight) * screenWidth / screenHeight);
            }
            else if (((double)screenWidth) / screenHeight < ((double)wWidth) / wHeight)
            {
                wHeight = (int)round(((double)wWidth) * screenHeight / screenWidth);
            }
            width = wWidth;
            height = wHeight;
        }
    }

	SDL_ShowCursor(0);
	Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    
    if (fullScreenMode == FullScreenMode::fullScreen) 
    {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    else if (fullScreenMode == FullScreenMode::windowFullScreen)
    {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    else
    {
        flags |= SDL_WINDOW_HIDDEN;
    }

	window = SDL_CreateWindow(windowCaption.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
	if (window == nullptr)
	{
		GameLog::write("SDL Create Window Error : %s\n", SDL_GetError());
		return sdlError;
	}

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);	

	realScreen = make_shared_image(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, wWidth, wHeight));
	
	screenMask = make_shared_surface(SDL_CreateRGBSurface(0, 1, 1, 32, 0, 0, 0, 0));

	initTime();
	
	updateState();

	loadLogo();

	SDL_ShowWindow(window);
	SDL_RaiseWindow(window);

	fadeInLogo();

//#ifdef __ANDROID__
//	std::string path = VIDEO_FOLDER;
//	std::string listName = path + SAVE_LIST_FILE;
//	INIReader ini(listName);
//	auto count = ini.GetInteger(SAVE_LIST_SECTION, SAVE_LIST_COUNT_KEY, 0);
//	for (int i = 0; i < count; ++i)
//	{
//		auto videoName = ini.Get(SAVE_LIST_SECTION, convert::formatString("%d", i), "");
//		std::string newName = path + videoName;
//		convert::replaceAllString(newName, "\\", "/");
//		convert::replaceAllString(newName, "/", "_");
//		if (!File::fileExist(newName))
//		{
//            File::copy(path + videoName, newName);
//		}
//	}
//#endif

	TTF_Init();

	clearCursor();

	initTime();

	return initOK;
}

void EngineBase::destroySDL()
{
	GameLog::write("Begin destroy SDL\n");
	destroyCursor();
	if (fontBuffer != nullptr)
	{
		fontBuffer = nullptr;
	}
	if (fontData != nullptr)
	{
		SDL_FreeRW(fontData);
		fontData = nullptr;
	}
	/*if (screenMask)
	{
		SDL_FreeSurface((SDL_Surface *)screenMask);
		screenMask = nullptr;
	}*/
	//if (realScreen)
	//{
	//	SDL_DestroyTexture(realScreen);
	//	realScreen = nullptr;
	//}
    GameLog::write("Begin destroy SDL Renderer \n");
    SDL_SetRenderTarget(renderer, nullptr);
    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    
    GameLog::write("Begin destroy SDL Window \n");
    if (window)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    
    
	SDL_Quit();
	GameLog::write("Destroy SDL done!\n");
}

void EngineBase::destroyCursor()
{
	clearCursor();
}

void EngineBase::updateState()
{
    int tempWidth = 0;
    int tempHeight = 0;
    SDL_GetWindowSize(window, &tempWidth, &tempHeight);
	updateRect(tempWidth, tempHeight, rect);
#ifdef __APPLE__
    SDL_Metal_GetDrawableSize(window, &tempWidth, &tempHeight);
    updateRect(tempWidth, tempHeight, displayRect);
#else
    displayRect = rect;
#endif
    updateCursor();
}
#ifdef SHF_USE_AUDIO
int EngineBase::initSoundSystem()
{
    if (FMOD_System_Create(&soundSystem, FMOD_VERSION) != 0)
	{
		return soundError;
	}
	if (FMOD_System_Init(soundSystem, maxChannel, FMOD_INIT_NORMAL, nullptr) != 0)
	{
		return soundError;
	}
	if (FMOD_System_Set3DSettings(soundSystem, 1, 1, 1) != 0)
	{
		return soundError;
	}
	FMOD_VECTOR pos = { 0, 0, 0 };
	FMOD_VECTOR vel = { 0, 0, 0 };
	FMOD_VECTOR forward = { 0, 0, 1 };
	FMOD_VECTOR up = { 0, 1, 0 };
	if (FMOD_System_Set3DListenerAttributes(soundSystem, 0, &pos, &vel, &forward, &up) != 0)
	{
		return soundError;
	}
	return 0;
}

void EngineBase::destroySoundSystem()
{
	GameLog::write("Begin destroy sound system\n");
	//soundMutex.lock();
	for (size_t i = 0; i < soundList.sound.size(); i++)
	{
		//stopMusic(soundList.sound[i].c);
		if (soundList.sound[i].m != nullptr)
		{
			freeMusic(soundList.sound[i].m);
		}
	}
	soundList.sound.resize(0);
	//FMOD_System_Update(soundSystem);
	//soundMutex.unlock();
	if (soundSystem != nullptr)
	{		
		FMOD_System_Close(soundSystem);
		FMOD_System_Release(soundSystem);
		soundSystem = nullptr;
	}
	GameLog::write("Destroy sound system done!\n");
}

void EngineBase::updateSoundSystem()
{
	FMOD_System_Update(soundSystem);
}
#endif

#ifdef SHF_USE_AUDIO
FMOD_RESULT F_CALLBACK EngineBase::autoReleaseSound(FMOD_CHANNELCONTROL * chanControl, FMOD_CHANNELCONTROL_TYPE controlType, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType, void * commandData1, void * commandData2)
{
	if (controlType == FMOD_CHANNELCONTROL_CHANNEL && callbackType == FMOD_CHANNELCONTROL_CALLBACK_END)
	{
		_channel channel = (_channel)chanControl;
		if (channel == nullptr)
		{
			return FMOD_OK;
			//return FMOD_RESULT_FORCEINT;
		}
		int index = -1;

		std::lock_guard<std::mutex> locker(Engine::getInstance()->soundMutex);

		if (soundList.sound.size() == 0)
		{

			return FMOD_OK;
		}
		for (size_t i = 0; i < soundList.sound.size(); i++)
		{
			if (soundList.sound[i].c == channel)
			{
				index = i;
				soundList.sound[i].stopped = true;
				//freeMusic(soundList.sound[i].m);
			}
		}

		if (index < 0)
		{
			return FMOD_OK;
		}
		//soundList.sound.erase(soundList.sound.begin() + index);

	}

	return FMOD_OK;
}
#endif

_music EngineBase::createMusic(const std::unique_ptr<char[]>& data, int size, bool loop, bool music3d, unsigned char priority)
{
#ifdef SHF_USE_AUDIO
	FMOD_SOUND * sound;
	FMOD_MODE mode;
	FMOD_CREATESOUNDEXINFO exinfo;
	mode = FMOD_OPENMEMORY | FMOD_CREATECOMPRESSEDSAMPLE;
	if (loop)
	{
		mode |= FMOD_LOOP_NORMAL;
	}
	else
	{
		mode |= FMOD_LOOP_OFF;
	}
	if (music3d)
	{
		mode |= FMOD_3D;		
	}
	else
	{
		mode |= FMOD_2D;
	}
	memset(&exinfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
	exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
	exinfo.length = size;

	if (FMOD_System_CreateSound(soundSystem, data.get(), mode, &exinfo, &sound) != 0)
	{
		//GameLog::write("Create Sound Error!\n");
		return nullptr;
	}
	if (priority != 128)
	{
		float f;
		FMOD_Sound_GetDefaults(sound, &f, nullptr);
		FMOD_Sound_SetDefaults(sound, f, priority);
	}
	if (music3d)
	{
		if (FMOD_Sound_Set3DMinMaxDistance(sound, 0.5f, 5000.0f) != 0)
		{
			GameLog::write("Set3DMinMaxDistance Error!\n");
		}
	}

	return (_music)sound;
#else
    return nullptr;
#endif

}
#ifdef SHF_USE_VIDEO
#ifdef SHF_USE_AUDIO
_music EngineBase::createVideoRAW(FMOD_SYSTEM * system, char * data, int size, bool loop, bool music3d, FMOD_SOUND_FORMAT format, int numchannels, int defaultfrequency, unsigned char priority)
{
	FMOD_SOUND * sound;
	FMOD_MODE mode;
	FMOD_CREATESOUNDEXINFO exinfo;
	mode = FMOD_OPENMEMORY | FMOD_OPENRAW ;
	if (loop)
	{
		mode |= FMOD_LOOP_NORMAL;
	}
	else
	{
		mode |= FMOD_LOOP_OFF;
	}
	if (music3d)
	{
		mode |= FMOD_3D;
	}
	else
	{
		mode |= FMOD_2D;
	}
	memset(&exinfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
	exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
	exinfo.length = size;
	exinfo.format = format; //FMOD_SOUND_FORMAT_PCM16
	exinfo.numchannels = numchannels;
	exinfo.defaultfrequency = defaultfrequency;
	exinfo.suggestedsoundtype = FMOD_SOUND_TYPE_RAW;
	FMOD_SYSTEM * tempSystem = system;
	if (tempSystem == nullptr)
	{
		tempSystem = soundSystem;
	}
	int ret = FMOD_System_CreateSound(tempSystem, data, mode, &exinfo, &sound);
	if (ret != 0)
	{
		//GameLog::write("Create Sound Error! %d\n", ret);
		return nullptr;
	}
	if (priority != 128)
	{
		float f;
		FMOD_Sound_GetDefaults(sound, &f, nullptr);
		FMOD_Sound_SetDefaults(sound, f, priority);
	}
	if (music3d)
	{
		if (FMOD_Sound_Set3DMinMaxDistance(sound, 0.5f, 5000.0f) != 0)
		{
			GameLog::write("Set3DMinMaxDistance Error!\n");
		}
	}
	return (_music)sound;
}
#endif
#endif

void EngineBase::freeMusic(_music music)
{
#ifdef SHF_USE_AUDIO
	if (music == nullptr)
	{
		GameLog::write("music to release is nullptr \n");
		return;
	}
	FMOD_Sound_Release(music);
#endif
}

_channel EngineBase::playMusic(_music music, float volume)
{
#ifdef SHF_USE_AUDIO
	if (music == nullptr)
	{
		return nullptr;
	}
	FMOD_CHANNEL* fmod_channel_;
	if (FMOD_System_PlaySound(soundSystem, music, nullptr, true, &fmod_channel_) != 0)
	{
		GameLog::write("Play Sound Error!\n");
	}
	_channel channel = fmod_channel_;
	setMusicVolume(channel, volume);
	resumeMusic(channel);
	return channel;
#else
	return nullptr;
#endif

}
_channel EngineBase::playMusic(_music music, float x, float y, float volume)
{
#ifdef SHF_USE_AUDIO
	if (music == nullptr)
	{
		return nullptr;
	}
	FMOD_CHANNEL * fmod_channel_;
	FMOD_System_PlaySound(soundSystem, music, nullptr, true, &fmod_channel_);
	_channel channel = fmod_channel_;
	setMusicPosition(channel, x, y);
	setMusicVolume(channel, volume);
	resumeMusic(channel);
	return channel;
#else
	return nullptr;
#endif
}

void EngineBase::stopMusic(_channel channel)
{
#ifdef SHF_USE_AUDIO
	FMOD_Channel_Stop(channel);
#endif
}

void EngineBase::pauseMusic(_channel channel)
{
#ifdef SHF_USE_AUDIO
	FMOD_Channel_SetPaused(channel, true);
#endif
}

void EngineBase::resumeMusic(_channel channel)
{
#ifdef SHF_USE_AUDIO
	FMOD_Channel_SetPaused(channel, false);
#endif
}

void EngineBase::setMusicPosition(_channel channel, float x, float y)
{
#ifdef SHF_USE_AUDIO
	FMOD_VECTOR vector;
	vector.x = x;
	vector.y = -1.0f;
	vector.z = y;
	FMOD_VECTOR vel = { 0.0f, 0.0f, 0.0f };
	FMOD_Channel_Set3DAttributes(channel, &vector, &vel);
#endif
}

void EngineBase::setMusicVolume(_channel channel, float volume)
{
#ifdef SHF_USE_AUDIO
	FMOD_Channel_SetVolume(channel, volume);
#endif
}

bool EngineBase::getMusicPlaying(_channel channel)
{
#ifdef SHF_USE_AUDIO
	FMOD_BOOL playing = false;
	if (FMOD_Channel_IsPlaying(channel, &playing) == 0)
	{
		return playing != 0;
	}
#endif
	return false;
}

bool EngineBase::soundAutoRelease(_music music, _channel channel)
{
#ifdef SHF_USE_AUDIO
	std::lock_guard<std::mutex> locker(soundMutex);
	for (size_t i = 0; i < soundList.sound.size(); i++)
	{
		if (soundList.sound[i].c == channel || soundList.sound[i].m == music)
		{
			return false;
		}
	}
	soundList.sound.resize(soundList.sound.size() + 1);
	soundList.sound[soundList.sound.size() - 1].c = channel;
	soundList.sound[soundList.sound.size() - 1].m = music;
	soundList.sound[soundList.sound.size() - 1].stopped = false;
	FMOD_Channel_SetCallback(soundList.sound[soundList.sound.size() - 1].c, autoReleaseSound);
#endif
	return true;
}

void EngineBase::checkSoundRelease()
{
#ifdef SHF_USE_AUDIO

	std::lock_guard<std::mutex> locker(soundMutex);
	for (size_t i = soundList.sound.size(); i > 0; --i)
	{
		FMOD_BOOL playing = 0;
		if (((FMOD_Channel_IsPlaying(soundList.sound[i - 1].c, &playing) == FMOD_OK) && !playing) || soundList.sound[i - 1].stopped)
		{
			auto c = soundList.sound[i - 1].c;
			auto m = soundList.sound[i - 1].m;

//#if (!defined _WIN32) && (!defined __ANDROID__)
			soundMutex.unlock();
//#endif // !_WIN32 && !__ANDROID__

			// linux系统下，此处有概率会再次进入callback，会有死锁，故这里解锁，其实单线程不需要锁
			// TODO: MAC 和 IOS 待验证
			FMOD_Channel_Stop(c);

//#if (!defined _WIN32) && (!defined __ANDROID__)
			soundMutex.lock();
//#endif //!_WIN32 && !__ANDROID__

			FMOD_Sound_Release(m);
			soundList.sound.erase(soundList.sound.begin() + i - 1);
		}
	}

#endif
}

#ifdef SHF_USE_VIDEO
int EngineBase::initVideo()
{
    avformat_network_init();
	clearVideoList();
	return 0;
}

void EngineBase::destroyVideo()
{
	GameLog::write("Begin destroy video\n");
	clearVideoList();
	GameLog::write("Destroy video done!\n");
}
#endif

#ifdef SHF_USE_VIDEO
void EngineBase::freeMediaStream(MediaStream * mediaStream)
{
	if (mediaStream == nullptr)
	{
		return;
	}
	if (mediaStream->frame != nullptr)
	{
		av_frame_free(&mediaStream->frame);
	}

	if (mediaStream->codecCtx != nullptr)
	{
        avcodec_close(mediaStream->codecCtx);
		avcodec_free_context(&mediaStream->codecCtx);
	}

	if (mediaStream->formatCtx != nullptr)
	{
		avformat_close_input(&mediaStream->formatCtx);
	}

    if (mediaStream->rWops != nullptr)
    {
        SDL_RWclose(mediaStream->rWops);
        mediaStream->rWops = nullptr;
    }
}
#endif

#ifdef SHF_USE_VIDEO
int EngineBase::openVideoFile(_video video)
{
	int result = -1;
	if (video == nullptr)
	{
		return result;
	}
	if (!File::fileExist(video->fileName))
	{
		return result;
	}

	setMediaStream(&video->videoStream, video->fileName, AVMEDIA_TYPE_VIDEO);
	setMediaStream(&video->audioStream, video->fileName, AVMEDIA_TYPE_AUDIO);
	if (video->videoStream.exists)
	{
		video->pixelFormat = getVideoPixelFormat(video->videoStream.codecCtx->pix_fmt);
	}
	if (video->audioStream.exists)
	{
		video->totalTime = video->audioStream.totalTime;
		result = 1;
	}
	else if (video->videoStream.exists)
	{
		video->totalTime = video->videoStream.totalTime;
		result = 1;
	}
	else
	{
		video->totalTime = 0;
	}
	return result;

}

int EngineBase::read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    auto fp = ((MediaStream*)opaque)->rWops;
    auto nowPos = SDL_RWtell(fp);
    int length = ((MediaStream*)opaque)->rWops_length;
    int newSize = buf_size;
    if (length - nowPos < buf_size)
    {
        newSize = (int)(length - nowPos);
    }
    auto sz = SDL_RWread(fp, buf, 1, newSize);
	if (sz == 0)
	{
        ((MediaStream*)opaque)->formatCtx->pb->eof_reached = 1;
	}
    return sz;
}

void EngineBase::setMediaStream(MediaStream * mediaStream, std::string& fileName, AVMediaType mediaType)
{
	if (mediaStream == nullptr)
	{
		return;
	}
	auto newFileName = fileName;

	if (!File::fileExist(newFileName))
	{
		return;
	}
    int ret = 0;

#if defined( __ANDROID__ )
	std::string path = SDL_AndroidGetInternalStoragePath();
	if (path.length() > 0 && *path.end() != '/') { path += '/'; }
	convert::replaceAllString(newFileName, "\\", "/");
//    convert::replaceAllString(newFileName, "/", "_");
//	newFileName = path + newFileName;
    auto *pFile = SDL_RWFromFile(newFileName.c_str(), "rb");
    mediaStream->rWops = pFile;
    SDL_RWseek(pFile, 0, RW_SEEK_END);
    mediaStream->rWops_length = SDL_RWtell(pFile);
    SDL_RWseek(pFile, 0, RW_SEEK_SET);

    if(!pFile) {
        av_log(nullptr, AV_LOG_ERROR, "Cannot open input file\n");
        return;
    }

    size_t buff_size = 10 * 1024;
    unsigned char * buff = new unsigned char[buff_size];

    AVIOContext *avio_ctx = avio_alloc_context(buff, buff_size, 0, mediaStream, read_packet, nullptr, nullptr);
    if(!avio_ctx) {
        ret = AVERROR(ENOMEM);
        return;
    }

    mediaStream->formatCtx->pb = avio_ctx;
    ret = avformat_open_input(&mediaStream->formatCtx, nullptr, nullptr, nullptr);
#elif defined( __APPLE__ )
    auto *pFile = SDL_RWFromFile(File::getAssetsName(newFileName).c_str(), "rb");
    mediaStream->rWops = pFile;
    SDL_RWseek(pFile, 0, RW_SEEK_END);
    mediaStream->rWops_length = SDL_RWtell(pFile);
    SDL_RWseek(pFile, 0, RW_SEEK_SET);

    if(!pFile) {
        av_log(nullptr, AV_LOG_ERROR, "Cannot open input file\n");
        return;
    }

    size_t buff_size = 10 * 1024;
    unsigned char * buff = new unsigned char[buff_size];

    AVIOContext *avio_ctx = avio_alloc_context(buff, buff_size, 0, mediaStream, read_packet, nullptr, nullptr);
    if(!avio_ctx) {
        ret = AVERROR(ENOMEM);
        return;
    }

    mediaStream->formatCtx->pb = avio_ctx;
    ret = avformat_open_input(&mediaStream->formatCtx, nullptr, nullptr, nullptr);
#else
    ret = avformat_open_input(&mediaStream->formatCtx, File::getAssetsName(newFileName).c_str(), nullptr, nullptr);
#endif

	if (ret != 0)
	{
		char buf[1024];
		av_strerror(ret, buf, 1024);
		GameLog::write("video %s open error: %s\n", File::getAssetsName(newFileName).c_str(), buf);
	}
	if (ret == 0)
	{
		ret = avformat_find_stream_info(mediaStream->formatCtx, nullptr);
		if (ret >= 0)
		{
			for (size_t i = 0; i < mediaStream->formatCtx->nb_streams; i++)
			{
				auto stream = mediaStream->formatCtx->streams[i];
				if (mediaStream->formatCtx->streams[i]->codecpar != nullptr && mediaStream->formatCtx->streams[i]->codecpar->codec_type == mediaType)
				{
					mediaStream->exists = true;
					mediaStream->stream = mediaStream->formatCtx->streams[i];
					if (mediaStream->stream->r_frame_rate.den)
					{
						mediaStream->timePerFrame = 1e3 / av_q2d(mediaStream->stream->r_frame_rate);
					}
					mediaStream->timeBasePacket = 1e3 * av_q2d(mediaStream->stream->time_base);
					mediaStream->totalTime = mediaStream->formatCtx->duration * 1e3 / AV_TIME_BASE;
					mediaStream->startTime = mediaStream->formatCtx->start_time * 1e3 / AV_TIME_BASE;
					mediaStream->index = i;
                    auto codec = avcodec_find_decoder(mediaStream->stream->codecpar->codec_id);
					mediaStream->codecCtx = avcodec_alloc_context3(codec);
					avcodec_parameters_to_context(mediaStream->codecCtx, mediaStream->stream->codecpar);
					//av_codec_set_pkt_timebase(mediaStream->codecCtx, mediaStream->stream->time_base);
					avcodec_open2(mediaStream->codecCtx, codec, nullptr);
					break;
				}
			}			
		}
	}	
}
#endif

double EngineBase::getVideoTime(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr || video->cg == nullptr)
	{
		return 0.0;
	}
	if (video->time.paused)
	{
		return video->time.pauseBeginTime - video->time.beginTime;
	}
	else
	{
		unsigned long long t = 0;
		if (FMOD_ChannelGroup_GetDSPClock(video->cg, 0, &t) == 0)
		{
			return ((double)t) / video->soundRate - video->time.beginTime;
		}
	}
#endif
	return 0.0;
}

#ifdef SHF_USE_VIDEO
double EngineBase::initVideoTime(_video video)
{
	if (video == nullptr || video->cg == nullptr)
	{
		return 0.0;
	}
	unsigned long long t = 0;
	if (FMOD_ChannelGroup_GetDSPClock(video->cg, 0, &t) == 0)
	{
		video->time.beginTime = ((double)t) / video->soundRate;
		video->time.pauseBeginTime = 0.0;
		video->time.paused = false;
		return video->time.beginTime;
	}
	return 0.0;
}

void EngineBase::setVideoTimePaused(_video video, bool paused)
{
	if (video == nullptr || video->cg == nullptr)
	{
		return;
	}
	if (video->time.paused == paused)
	{
		return;
	}
	unsigned long long t = 0;
	if (FMOD_ChannelGroup_GetDSPClock(video->cg, 0, &t) == 0)
	{
		double now = ((double)t) / video->soundRate;
		if (paused)
		{
			video->time.pauseBeginTime = now;
			video->time.paused = true;
		}
		else
		{
			video->time.beginTime += now - video->time.pauseBeginTime;
			video->time.paused = false;
		}
	}
}

double EngineBase::setVideoTime(_video video, double timer)
{
	if (video == nullptr || video->cg == nullptr)
	{
		return 0.0;
	}
	video->time.beginTime += getVideoTime(video) - timer;
	return getVideoTime(video);
}

double EngineBase::getVideoSoundRate(_video video)
{
	int outputRate = 0;
	if (video == nullptr || video->soundSystem == nullptr)
	{
		FMOD_System_GetSoftwareFormat(soundSystem, &outputRate, 0, 0);
	}
	else
	{
		FMOD_System_GetSoftwareFormat(video->soundSystem, &outputRate, 0, 0);
	}
	return ((double)outputRate) / 1000.0;

}

void EngineBase::decodeNextAudio(_video video)
{
#ifdef SHF_USE_AUDIO
	if (video == nullptr || !video->audioStream.exists)
	{
		return;
	}
	int ret = 0;
	while (ret == 0)
	{
		video->audioStream.packet = av_packet_alloc();
        bool haveFrame = false;
#ifdef __ANDROID__
        int nowPos = SDL_RWtell(video->audioStream.rWops);
        if (nowPos >= video->audioStream.rWops_length)
        {
            haveFrame = false;
        }
        else
        {
            haveFrame = (av_read_frame(video->audioStream.formatCtx, video->audioStream.packet) >= 0);
        }
#else
		haveFrame = (av_read_frame(video->audioStream.formatCtx, video->audioStream.packet) >= 0);
#endif
		if (!haveFrame)
		{
			video->audioStream.decodeEnd = true;
			av_packet_free(&video->audioStream.packet);
			break;
		}
		if (video->audioStream.packet->stream_index == video->audioStream.index)
		{
			//循环处理多次才能解到一帧的情况
			int gotframe = 0;
			int gotsize = 0;
			int state = 0;

			int64_t pts = 0;
			int64_t dts = 0;
			if (avcodec_send_packet(video->audioStream.codecCtx, video->audioStream.packet) == 0)
			{			
				if (!video->audioStream.setTS)
				{
					pts = video->audioStream.packet->pts;
					dts = video->audioStream.packet->dts;
					video->audioStream.setTS = true;
				}
				while (avcodec_receive_frame(video->audioStream.codecCtx, video->audioStream.frame) == 0)
				{
					video->audioStream.setTS = false;
					ret = 1;
					AVFrame * f = video->audioStream.frame;
#if (defined USE_FFMPEG4)
					int data_length_ = convert(video->audioStream.codecCtx, video->audioStream.frame, AV_SAMPLE_FMT_S16, video->audioStream.codecCtx->sample_rate, video->audioStream.codecCtx->channel_layout, (uint8_t*)video->buffer);
#else
					int data_length_ = convert(video->audioStream.codecCtx, video->audioStream.frame, AV_SAMPLE_FMT_S16, video->audioStream.codecCtx->sample_rate, video->audioStream.codecCtx->ch_layout.nb_channels, (uint8_t *)video->buffer);
#endif					
					if (data_length_ > 0)
					{
						if (video->soundSystem == nullptr)
						{
							if (FMOD_System_Create(&video->soundSystem, FMOD_VERSION) == 0
								&& FMOD_System_SetSoftwareFormat(video->soundSystem, video->audioStream.codecCtx->sample_rate, FMOD_SPEAKERMODE_DEFAULT, 0) == 0 &&
								FMOD_System_Init(video->soundSystem, 64, FMOD_INIT_NORMAL, nullptr) == 0)
							{
								FMOD_ChannelGroup_Release(video->cg);
								FMOD_System_CreateChannelGroup(video->soundSystem, nullptr, &video->cg);
								video->soundRate = getVideoSoundRate(video);
								initVideoTime(video);
								setVideoTimePaused(video, true);
							}
						}
						FMOD_SYSTEM * tempSystem = video->soundSystem;
						if (tempSystem == nullptr)
						{
							tempSystem = soundSystem;
						}
#if (defined USE_FFMPEG4)
						_music m = createVideoRAW(tempSystem, video->buffer, data_length_, false, false, FMOD_SOUND_FORMAT_PCM16, video->audioStream.codecCtx->channel_layout, video->audioStream.codecCtx->sample_rate, 1);
#else
						_music m = createVideoRAW(tempSystem, video->buffer, data_length_, false, false, FMOD_SOUND_FORMAT_PCM16, video->audioStream.codecCtx->ch_layout.nb_channels, video->audioStream.codecCtx->sample_rate, 1);
#endif // (defined USE_FFMPEG4)

						VideoSound videoSound;
						//videoSound.t = ((double)pts * video->audioStream.timeBasePacket);
						videoSound.t = video->soundDelay + video->audioStream.startTime;

#if (defined USE_FFMPEG4)
						double addTime = ((double)data_length_) / 2.0 / (((double)video->audioStream.codecCtx->sample_rate) / 1000.0) / ((double)video->audioStream.codecCtx->channel_layout);
#else
						double addTime = ((double)data_length_) / 2.0 / (((double)video->audioStream.codecCtx->sample_rate) / 1000.0) / ((double)video->audioStream.codecCtx->ch_layout.nb_channels);
#endif // (defined USE_FFMPEG4)		

						video->soundDelay += addTime;
						videoSound.m = m;
						FMOD_CHANNEL * c;
						if (FMOD_System_PlaySound(tempSystem, m, video->cg, true, &c) == 0)
						{
							videoSound.c = c;
							setMusicVolume(c, video->videoVolume);
							FMOD_Channel_SetCallback(c, audioCallback);
							if (video->running)
							{
								if ((videoSound.t - getVideoTime(video)) < 0)
								{
									setVideoTime(video, (videoSound.t));
								}
								unsigned long long clock_start = 0;
								FMOD_Channel_GetDSPClock(c, 0, &clock_start);
								clock_start += (unsigned long long)((videoSound.t - getVideoTime(video)) * video->soundRate + 0.5);
								FMOD_Channel_SetDelay(c, clock_start, 0, true);
								if (!video->time.paused)
								{
									resumeMusic(c);
								}
							}
						}
						else
						{
							videoSound.c = nullptr;
						}
						video->videoSounds.push_back(videoSound);
					}
				}
			}			
		}
		av_packet_free(&video->audioStream.packet);
	}
#endif
}

void EngineBase::decodeNextVideo(_video video)
{
	if (video == nullptr || !video->videoStream.exists)
	{
		return;
	}
	int ret = 0;
	while (ret == 0)
	{
		video->videoStream.packet = av_packet_alloc();
//		bool haveFrame = (av_read_frame(video->videoStream.formatCtx, video->videoStream.packet) >= 0);
        bool haveFrame = false;
#ifdef __ANDROID__
        int nowPos = SDL_RWtell(video->videoStream.rWops);
        if (nowPos >= video->videoStream.rWops_length)
        {
            haveFrame = false;
        }
        else
        {
            haveFrame = (av_read_frame(video->videoStream.formatCtx, video->videoStream.packet) >= 0);
        }
#else
        haveFrame = (av_read_frame(video->videoStream.formatCtx, video->videoStream.packet) >= 0);
#endif
		if (!haveFrame)
		{
			video->videoStream.decodeEnd = true;
			break;
		}
		if (video->videoStream.packet->stream_index == video->videoStream.index)
		{
			int gotframe = 0;
			int gotsize = 0;
			int state = 0;

			int64_t pts = 0;
			int64_t dts = 0;

			if (avcodec_send_packet(video->videoStream.codecCtx, video->videoStream.packet) == 0)
			{
				if (!video->videoStream.setTS)
				{
					pts = video->videoStream.packet->pts;
					dts = video->videoStream.packet->dts;
					video->videoStream.setTS = true;
				}
				while (avcodec_receive_frame(video->videoStream.codecCtx, video->videoStream.frame) == 0)
				{
					video->videoStream.setTS = false;
					ret = 2;
//					if (video->swsContext == nullptr)
//					{
//						video->swsContext = sws_getContext(video->videoStream.codecCtx->width, video->videoStream.codecCtx->height, video->videoStream.codecCtx->pix_fmt, video->videoStream.codecCtx->width, video->videoStream.codecCtx->height, AV_PIX_FMT_YUV420P/*AV_PIX_FMT_RGB24*/, SWS_BICUBIC, nullptr, nullptr, nullptr);
//						video->b = av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P/*AV_PIX_FMT_RGB24*/, video->videoStream.codecCtx->width, video->videoStream.codecCtx->height, 1));
//						av_image_fill_arrays(video->sFrame->data, video->sFrame->linesize, (const uint8_t *)video->b, AV_PIX_FMT_YUV420P/*AV_PIX_FMT_RGB24*/, video->videoStream.codecCtx->width, video->videoStream.codecCtx->height, 1);
//					}
					AVFrame * f = video->videoStream.frame;
					auto tex = make_shared_image(SDL_CreateTexture(renderer, (Uint32)video->pixelFormat, SDL_TEXTUREACCESS_STREAMING, video->videoStream.codecCtx->width, video->videoStream.codecCtx->height));
					switch ((Uint32)video->pixelFormat)
					{
					case SDL_PIXELFORMAT_UNKNOWN:
						video->swsContext =	sws_getCachedContext(video->swsContext, f->width, f->height, AVPixelFormat(f->format), f->width, f->height, AV_PIX_FMT_BGRA, SWS_BICUBIC, nullptr, nullptr, nullptr);
						if (video->swsContext)
						{
							uint8_t* pixels[4];
							int pitch[4];
							if (!SDL_LockTexture(tex.get(), nullptr, (void**)pixels, pitch))
							{
								sws_scale(video->swsContext, (const uint8_t * const*)f->data, f->linesize, 0, f->height, pixels, pitch);
								SDL_UnlockTexture(tex.get());
							}
						}
						break;
					case SDL_PIXELFORMAT_IYUV:
						if (f->linesize[0] > 0 && f->linesize[1] > 0 && f->linesize[2] > 0)
						{
//							SDL_UpdateYUVTexture(tex.get(), nullptr, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2], f->linesize[2]);
#ifdef __ANDROID__
							SDL_UpdateYUVTexture(tex.get(), nullptr, f->data[0], f->linesize[0], f->data[2], f->linesize[2], f->data[1], f->linesize[1]);
#else
							SDL_UpdateYUVTexture(tex.get(), nullptr, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2], f->linesize[2]);
#endif
                        }
						else if (f->linesize[0] < 0 && f->linesize[1] < 0 && f->linesize[2] < 0)
						{
							SDL_UpdateYUVTexture(tex.get(), nullptr,
								f->data[0] + f->linesize[0] * (f->height - 1), -f->linesize[0],
								f->data[1] + f->linesize[1] * (AV_CEIL_RSHIFT(f->height, 1) - 1), -f->linesize[1],
								f->data[2] + f->linesize[2] * (AV_CEIL_RSHIFT(f->height, 1) - 1), -f->linesize[2]);
						}
						else
						{
							GameLog::write("Mixed negative and positive line sizes are not supported.\n");
						}
//						sws_scale(video->swsContext, (const uint8_t * const*)f->data, f->linesize, 0, video->videoStream.codecCtx->height, (uint8_t * const*)video->sFrame->data, video->sFrame->linesize);
//						SDL_UpdateYUVTexture(tex, nullptr, video->sFrame->data[0], video->sFrame->linesize[0], video->sFrame->data[1], video->sFrame->linesize[1], video->sFrame->data[2], video->sFrame->linesize[2]);
						break;
					default:
						if (f->linesize[0] < 0)
						{
							SDL_UpdateTexture(tex.get(), nullptr, f->data[0] + f->linesize[0] * (f->height - 1), -f->linesize[0]);
						}
						else
						{
							SDL_UpdateTexture(tex.get(), nullptr, f->data[0], f->linesize[0]);
						}
						break;
					}
					VideoImage videoImage;
					videoImage.t = (double)((dts > 0 ? dts : pts)) * video->videoStream.timeBasePacket + video->videoStream.startTime;
					videoImage.image = tex;
					video->videoImage.push_back(videoImage);
				}
			}
				
		}
		av_packet_free(&video->videoStream.packet);
	}
}

void EngineBase::checkVideoDecodeEnd(_video video)
{
	if (video == nullptr)
	{
		return;
	}
	if ((video->audioStream.exists && !video->audioStream.decodeEnd) || (video->videoStream.exists && !video->videoStream.decodeEnd))
	{
		video->decodeEnd = false;
	}
	else
	{
		video->decodeEnd = true;
	}
}
#endif

bool EngineBase::getVideoStopped(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return true;
	}
	return video->stopped;
#else
    return true;
#endif
}

#ifdef SHF_USE_VIDEO
void EngineBase::pauseAllVideo()
{
	for (size_t i = 0; i < videoList.size(); i++)
	{
		if (videoList[i] == nullptr)
		{
			continue;
		}
		if (videoList[i]->time.paused)
		{
			videoList[i]->pausedBeforePause = true;
		}
		else
		{
			videoList[i]->pausedBeforePause = false;
			pauseVideo(videoList[i]);
		}
	}
}

void EngineBase::resumeAllVideo()
{
	for (size_t i = 0; i < videoList.size(); i++)
	{
		if (videoList[i] == nullptr)
		{
			continue;
		}
		if (!videoList[i]->pausedBeforePause)
		{
			resumeVideo(videoList[i]);
		}
	}
}

void EngineBase::clearVideo(_video video)
{
	if (video == nullptr)
	{
		return;
	}

	for (size_t i = 0; i < video->videoSounds.size(); i++)
	{
		stopMusic(video->videoSounds[i].c);
		freeMusic(video->videoSounds[i].m);
	}
	video->videoSounds.clear();
	//for (size_t i = 0; i < video->videoImage.size(); i++)
	//{
	//	freeImage(video->videoImage[i]);
	//}
	video->videoImage.clear();
}

void EngineBase::rearrangeVideoFrame(_video video)
{
	if (video == nullptr)
	{
		return;
	}
	for (int i = 0; i < (int)video->videoImage.size(); i++)
	{
		for (int j = 0; j < int(video->videoImage.size()) - i - 1; j--)
		{
			if (video->videoImage[j].t > video->videoImage[j + 1].t)
			{
				double t = video->videoImage[j].t;
				video->videoImage[j].t = video->videoImage[j + 1].t;
				video->videoImage[j + 1].t = t;
			}
		}
	}
}

int EngineBase::getVideoPixelFormat(int originalFormat)
{
	std::map<int, int> pix_ffmpeg_sdl =
	{
		{ AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },
		{ AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 },
		{ AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555 },
		{ AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555 },
		{ AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },
		{ AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 },
		{ AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },
		{ AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 },
		{ AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 },
		{ AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888 },
		{ AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
		{ AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
		{ AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 },
		{ AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 },
		{ AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 },
		{ AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 },
		{ AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },
		{ AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },
		{ AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },
		{ AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN },
	};
	if (pix_ffmpeg_sdl.count(originalFormat) > 0)
	{
		return pix_ffmpeg_sdl[originalFormat];
	}
	else
	{
		return 0;
	}	
}
#endif

void EngineBase::resetVideo(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	initVideoTime(video);
	setVideoTimePaused(video, true);
	clearVideo(video);

	if (video->videoStream.exists)
	{
		avcodec_flush_buffers(video->videoStream.codecCtx);
		av_seek_frame(video->videoStream.formatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
		video->videoStream.decodeEnd = false;
		video->videoStream.setTS = false;
	}
	if (video->audioStream.exists)
	{
		avcodec_flush_buffers(video->audioStream.codecCtx);
		av_seek_frame(video->audioStream.formatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
		video->audioStream.decodeEnd = false;
		video->audioStream.setTS = false;
	}

	video->soundDelay = 0.0;
	video->soundRate = getVideoSoundRate(video);
	video->pausedBeforePause = false;
	video->decodeEnd = false;
	video->stopped = false;
	if (video->running)
	{
		video->running = false;
		runVideo(video);
	}
#endif
}

void EngineBase::setVideoLoop(_video video, int loop)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	video->loop = loop;
#endif
}

#ifdef SHF_USE_VIDEO
FMOD_RESULT F_CALLBACK EngineBase::audioCallback(FMOD_CHANNELCONTROL * chanControl, FMOD_CHANNELCONTROL_TYPE controlType, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType, void * commandData1, void * commandData2)
{
	if (controlType == FMOD_CHANNELCONTROL_CHANNEL && callbackType == FMOD_CHANNELCONTROL_CALLBACK_END)
	{
		_channel channel = (FMOD_CHANNEL*)chanControl;
		if (channel == nullptr)
		{
			return FMOD_RESULT_FORCEINT;
		}
		int index = -1;
		int index2 = 0;
		for (size_t i = 0; i < videoList.size(); i++)
		{
			if (videoList[i]->videoSounds.size() > 0)
			{
				bool find = false;
				for (size_t j = 0; j < videoList[i]->videoSounds.size(); j++)
				{
					if (videoList[i]->videoSounds[j].c == channel)
					{
						find = true;
						index2 = j;
						break;
					}
				}
				if (find)
				{
					index = i;
					break;
				}			
			}
		}
		if (index >= 0)
		{
			for (int i = 0; i <= index2; i++)
			{
				//freeMusic(videoList[index]->videoSounds[i]);
				videoList[index]->videoSounds[i].stopped = true;
			}
		}
	}
	return FMOD_OK;
}
#endif

#ifdef SHF_USE_VIDEO
int EngineBase::convert(AVCodecContext * codecCtx, AVFrame * frame, int out_sample_format, int out_sample_rate, int out_channels, uint8_t * out_buf)
{
	SwrContext* swr_ctx = nullptr;
	int data_size = 0;
	int ret = 0;
#if (defined USE_FFMPEG4)
	int64_t src_ch_layout = codecCtx->channel_layout;
	int64_t dst_ch_layout = AV_CH_LAYOUT_STEREO;
#else
	AVChannelLayout dst_ch_layout;
#endif
	int dst_nb_channels = 0;
	int dst_linesize = 0;
	int src_nb_samples = 0;
	int dst_nb_samples = 0;
	int max_dst_nb_samples = 0;
	uint8_t** dst_data = nullptr;
	int resampled_data_size = 0;

	swr_ctx = swr_alloc();
	if (!swr_ctx)
	{
		GameLog::write("swr_alloc error \n");
		return -1;
	}
#if (defined USE_FFMPEG4)
	src_ch_layout = (codecCtx->channels ==
		av_get_channel_layout_nb_channels(codecCtx->channel_layout)) ?
		codecCtx->channel_layout : av_get_default_channel_layout(codecCtx->channels);
#endif
	
#if (defined USE_FFMPEG4)
	switch (out_channels)
	{
	case 1:
		dst_ch_layout = AV_CH_LAYOUT_MONO;
		break;
	case 2:
		dst_ch_layout = AV_CH_LAYOUT_STEREO;
		break;
	case 3:
		dst_ch_layout = AV_CH_LAYOUT_SURROUND;
		break;
	case 4:
		dst_ch_layout = AV_CH_LAYOUT_QUAD;
		break;
	case 5:
		dst_ch_layout = AV_CH_LAYOUT_5POINT0;
		break;
	case 6:
		dst_ch_layout = AV_CH_LAYOUT_5POINT1;
		break;
	}
#else
	//这里的设置很粗糙，最好详细处理
	switch (out_channels)
	{
	case 1:
		av_channel_layout_from_mask(&dst_ch_layout, AV_CH_LAYOUT_MONO);
		break;
	case 2:
		av_channel_layout_from_mask(&dst_ch_layout, AV_CH_LAYOUT_STEREO);
		break;
	case 3:
		av_channel_layout_from_mask(&dst_ch_layout, AV_CH_LAYOUT_SURROUND);
		break;
	case 4:
		av_channel_layout_from_mask(&dst_ch_layout, AV_CH_LAYOUT_QUAD);
		break;
	case 5:
		av_channel_layout_from_mask(&dst_ch_layout, AV_CH_LAYOUT_5POINT0);
		break;
	case 6:
		av_channel_layout_from_mask(&dst_ch_layout, AV_CH_LAYOUT_5POINT1);
		break;
	}
#endif
	src_nb_samples = frame->nb_samples;
	if (src_nb_samples <= 0)
	{
		GameLog::write("src_nb_samples error \n");
		return -1;
	}
#if (defined USE_FFMPEG4)
	av_opt_set_int(swr_ctx, "in_channel_layout", src_ch_layout, 0);
#else
	av_opt_set_chlayout(swr_ctx, "in_channel_layout", &codecCtx->ch_layout, 0);
#endif
	av_opt_set_int(swr_ctx, "in_sample_rate", codecCtx->sample_rate, 0);
	av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", codecCtx->sample_fmt, 0);
#if (defined USE_FFMPEG4)
	av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);
#else
	av_opt_set_chlayout(swr_ctx, "out_channel_layout", &dst_ch_layout, 0);
#endif
	av_opt_set_int(swr_ctx, "out_sample_rate", out_sample_rate, 0);
	av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", (AVSampleFormat)out_sample_format, 0);

	if ((ret = swr_init(swr_ctx)) < 0)
	{
		GameLog::write("Failed to initialize the resampling context\n");
		return -1;
	}

	max_dst_nb_samples = dst_nb_samples = (int)av_rescale_rnd(src_nb_samples, out_sample_rate, codecCtx->sample_rate, AV_ROUND_UP);
	if (max_dst_nb_samples <= 0)
	{
		GameLog::write("av_rescale_rnd error \n");
		return -1;
	}
#if (defined USE_FFMPEG4)
	dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);
#else
	dst_nb_channels = dst_ch_layout.nb_channels;
#endif
	ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels, dst_nb_samples, (AVSampleFormat)out_sample_format, 0);
	if (ret < 0)
	{
		GameLog::write("av_samples_alloc_array_and_samples error \n");
		return -1;
	}

	dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(swr_ctx, codecCtx->sample_rate) + src_nb_samples, out_sample_rate, codecCtx->sample_rate, AV_ROUND_UP);
	if (dst_nb_samples <= 0)
	{
		GameLog::write("av_rescale_rnd error \n");
		return -1;
	}
	if (dst_nb_samples > max_dst_nb_samples)
	{
		av_free(&dst_data[0]);
		ret = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels, dst_nb_samples, (AVSampleFormat)out_sample_format, 1);
		max_dst_nb_samples = dst_nb_samples;
	}

	if (swr_ctx)
	{
		ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
		if (ret < 0)
		{
			GameLog::write("swr_convert error \n");
			return -1;
		}

		resampled_data_size = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels, ret, (AVSampleFormat)out_sample_format, 1);
		if (resampled_data_size < 0)
		{
			GameLog::write("av_samples_get_buffer_size error \n");
			return -1;
		}
	}
	else
	{
		GameLog::write("swr_ctx null error \n");
		return -1;
	}

	memcpy(out_buf, dst_data[0], resampled_data_size);
	swr_close(swr_ctx);
#if (!defined USE_FFMPEG4)
	av_channel_layout_uninit(&dst_ch_layout);
#endif
	if (dst_data)
	{
		av_freep(&dst_data[0]);
	}
	av_freep(&dst_data);
	dst_data = nullptr;

	if (swr_ctx)
	{
		swr_free(&swr_ctx);
	}
	return resampled_data_size;
}
#endif

#ifdef SHF_USE_VIDEO
void EngineBase::clearVideoList()
{
	videoList.resize(0);
}

void EngineBase::addVideoToList(_video video)
{
	if (video == nullptr)
	{
		return;
	}
	videoList.push_back(video);
}

void EngineBase::deleteVideoFromList(_video video)
{
	if (video == nullptr)
	{
		return;
	}
	int index = -1;
	for (int i = 0; i < (int)videoList.size(); i++)
	{
		if (videoList[i] == video)
		{
			index = i;
			break;
		}
	}
	if (index >= 0)
	{
		deleteVideoFromList(index);
	}
}

void EngineBase::deleteVideoFromList(int index)
{
	if (index < 0 || index >= (int)videoList.size())
	{
		return;
	}
	videoList.erase(videoList.begin() + index);
}
#endif

_video EngineBase::loadVideo(const std::string& fileName)
{
#ifdef SHF_USE_VIDEO
	GameLog::write("Open video %s\n", fileName.c_str());
	if (!File::fileExist(fileName))
	{
		GameLog::write("Video:%s not exists\n", fileName.c_str());
		return nullptr;
	}

	auto video = new Video_t;
	video->videoImage.resize(0);
	video->videoSounds.resize(0);
	video->videoStream.formatCtx = avformat_alloc_context();
	video->audioStream.formatCtx = avformat_alloc_context();
	video->videoStream.frame = av_frame_alloc();
	video->audioStream.frame = av_frame_alloc();
	video->sFrame = av_frame_alloc();
 	video->buffer = (char *)av_mallocz(videoConvertSize);
	FMOD_System_CreateChannelGroup(soundSystem, nullptr, &video->cg);
	video->soundDelay = 0;
	video->soundRate = getVideoSoundRate(video);
	initVideoTime(video);
	setVideoTimePaused(video, true);
	video->fileName = fileName;
	setVideoRect((_video)video, nullptr);
	video->decodeEnd = false;
	video->videoVolume = 1;
	if (openVideoFile(video) < 0)
	{
		GameLog::write("Open video:%s error\n", fileName.c_str());
		return nullptr;
	}

	addVideoToList(video);

	return (_video)video;
#else
	return nullptr;
#endif
}

void EngineBase::setVideoRect(_video video, Rect * rect)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	if (rect == nullptr)
	{
		video->fullScreen = true;
	}
	else
	{
		video->fullScreen = false;
		video->rect.x = rect->x;
		video->rect.y = rect->y;
		video->rect.w = rect->w;
		video->rect.h = rect->h;
	}
#endif
}

void EngineBase::freeVideo(Video_t* video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	deleteVideoFromList(video);
	stopVideo(video);
	if (video->buffer != nullptr)
	{
		av_free(video->buffer);
		video->buffer = nullptr;
	}
	if (video->sFrame != nullptr)
	{
		av_frame_free(&video->sFrame);
		video->sFrame = nullptr;
	}
	if (video->swsContext != nullptr)
	{
		sws_freeContext(video->swsContext);
		video->swsContext = nullptr;
	}
	if (video->b != nullptr)
	{
		av_free(video->b);
		video->b = nullptr;
	}
	freeMediaStream(&video->audioStream);
	freeMediaStream(&video->videoStream);
	if (video->cg != nullptr)
	{
		FMOD_ChannelGroup_Release(video->cg);
		video->cg = nullptr;
	}
	for (size_t i = 0; i < video->videoSounds.size(); i++)
	{
		stopMusic(video->videoSounds[i].c);
		freeMusic(video->videoSounds[i].m);
	}

	if (video->soundSystem != nullptr)
	{
		FMOD_System_Close(video->soundSystem);
		FMOD_System_Release(video->soundSystem);
		video->soundSystem = nullptr;
	}
	delete video;
#endif
}

void EngineBase::runVideo(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr || video->running)
	{
		return;
	}
	video->stopped = false;
	initVideoTime(video);
	setVideoTimePaused(video, true);
	tryDecodeVideo(video);
	video->running = true;

	if (video->audioStream.exists)
	{
		if (video->videoSounds.size() > 0)
		{
			for (size_t i = 0; i < video->videoSounds.size(); i++)
			{
				unsigned long long clock_start = 0;
				FMOD_Channel_GetDSPClock(video->videoSounds[i].c, 0, &clock_start);
				clock_start += (unsigned long long)((video->videoSounds[i].t) * video->soundRate + 0.5);
				FMOD_Channel_SetDelay(video->videoSounds[i].c, clock_start, 0, true);
				resumeMusic(video->videoSounds[i].c);
			}
		}	
	}
	initVideoTime(video);
#endif
}

bool EngineBase::updateVideo(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return false;
	}
	if (video->soundSystem != nullptr)
	{
		FMOD_System_Update(video->soundSystem);
	}
	else
	{
		updateSoundSystem();
	}
	tryDecodeVideo(video);
	if (video->stopped)
	{
		if (video->loop > 0)
		{
			video->loop -= 1;
			resetVideo(video);
		}
		else if (video->loop < 0)
		{
			resetVideo(video);
		}
	}
	auto iter = video->videoSounds.begin();
	while (iter != video->videoSounds.end())
	{
		if (iter->stopped)
		{
			stopMusic(iter->c);
			freeMusic(iter->m);
			iter = video->videoSounds.erase(iter);
		}
		else
		{
			iter++;
		}
	}

#endif
	return true;
}

#ifdef SHF_USE_VIDEO
void EngineBase::tryDecodeVideo(_video video)
{
	if (video == nullptr)
	{
		return;
	}
	if (video->stopped)
	{
		return;
	}
	if (video->running && video->time.paused)
	{
		return;
	}
	if (getVideoTime(video) > video->totalTime)
	{
		video->stopped = true;
		clearVideo(video);
	}
	auto video_time = getVideoTime(video);
	if (video_time > video->lastTime + 200)
	{
		setVideoTime(video, video->lastTime + 200);
		video_time = video->lastTime + 200;
	}
	video->lastTime = video_time;
	if (video->audioStream.exists)
	{		
		if (video->videoStream.exists)
		{			
			while ((!video->audioStream.decodeEnd) && ((video->videoSounds.size() < (unsigned int)2) || (video->videoSounds.size() > 0 && (video->videoSounds[video->videoSounds.size() - 1].t < video_time + 100))))
			{
				decodeNextAudio(video);
				checkVideoDecodeEnd(video);
			}
			while ((!video->videoStream.decodeEnd) && ((video->videoImage.size() < (unsigned int)2) || (video->videoImage.size() > 0 && (video->videoImage[video->videoImage.size() - 1].t < video_time + 100))))
			{
				decodeNextVideo(video);
				checkVideoDecodeEnd(video);
			}
		}
		else
		{
			while ((!video->audioStream.decodeEnd) && ((video->videoSounds.size() < (unsigned int)2) || (video->videoSounds.size() > 0 && (video->videoSounds[video->videoSounds.size() - 1].t < video_time + 100))))
			{
				decodeNextAudio(video);
				checkVideoDecodeEnd(video);
			}
		}
	}
	else if (video->videoStream.exists)
	{
		while ((!video->videoStream.decodeEnd) && ((video->videoImage.size() < (unsigned int)2) || (video->videoImage.size() > 0 && (video->videoImage[video->videoImage.size() - 1].t < video_time + 100))))
		{
			decodeNextVideo(video);
			checkVideoDecodeEnd(video);
		}
	}
}
#endif

void EngineBase::drawVideoFrame(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	SDL_Rect* rect;
	if (video->fullScreen)
	{
		rect = nullptr;
	}
	else
	{
		rect = &video->rect;
	}
	//rearrangeVideoFrame(v);
	double t = getVideoTime(video);
	if (video->videoImage.size() == 0)
	{
		_shared_image image = createMask(0, 0, 0, 255);
		drawImage(image, rect);
		//freeImage(image);
	}
	else if (video->videoImage.size() == 1)
	{
		drawImage(video->videoImage[0].image, rect);
	}
	else if (video->videoImage.size() > 1)
	{
		int index = 0;
		for (size_t i = 0; i < video->videoImage.size(); i++)
		{
			if ((int)t >= video->videoImage[i].t)
			{
				index = i;
			}
		}

		drawImage(video->videoImage[index].image, rect);

		// TODO: 优化代码
		if (index > 0)
		{
			video->videoImage.erase(video->videoImage.begin(), video->videoImage.begin() + index - 1);
		}	
	}
#endif
}

bool EngineBase::onVideoFrame(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return false;
	}
	if (updateVideo(video))
	{
		drawVideoFrame(video);
		return true;
	}
#endif
	return false;
}

void EngineBase::pauseVideo(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	if (video->audioStream.exists)
	{
		for (size_t i = 0; i < video->videoSounds.size(); i++)
		{
			pauseMusic(video->videoSounds[i].c);
		}	
	}
	setVideoTimePaused(video, true);
#endif
}

void EngineBase::resumeVideo(_video video)
{
#ifdef SHF_USE_VIDEO

	if (video == nullptr)
	{
		return;
	}
	setVideoTimePaused(video, false);
	if (video->audioStream.exists)
	{		
		for (size_t i = 0; i < video->videoSounds.size(); i++)
		{
			if (video->videoSounds[i].t > getVideoTime(video))
			{
				unsigned long long clock_start = 0;
				FMOD_Channel_GetDSPClock(video->videoSounds[i].c, 0, &clock_start);
				clock_start += (unsigned long long)((video->videoSounds[i].t - getVideoTime(video)) * video->soundRate);
				FMOD_Channel_SetDelay(video->videoSounds[i].c, clock_start, 0, true);
			}
			resumeMusic(video->videoSounds[i].c);
		}
	}
#endif
}

void EngineBase::stopVideo(_video video)
{
#ifdef SHF_USE_VIDEO
	if (video == nullptr)
	{
		return;
	}
	video->running = false;
	resetVideo(video);
#endif
}

void EngineBase::frameBegin()
{	
	updateState();
	clearScreen();
	handleEvent();
}

void EngineBase::frameEnd()
{
#ifdef SHF_USE_AUDIO
	updateSoundSystem();
	checkSoundRelease();
#endif
	displayScreen();
	countFPS();
}

void EngineBase::clearScreen()
{
	SetRenderTarget(renderer, realScreen.get());
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);
}

void EngineBase::displayScreen()
{	
	unsigned int engineBaseBackCol = clBG;

	SetRenderTarget(renderer, nullptr);
	SDL_SetRenderDrawColor(renderer, (engineBaseBackCol & 0xFF0000) >> 16, (engineBaseBackCol & 0xFF00) >> 8, engineBaseBackCol & 0xFF, 0);
	SDL_RenderClear(renderer);
	SDL_Rect s, d;
	s.x = 0;
	s.y = 0;
	s.h = height;
	s.w = width;
	d.x = displayRect.x;
	d.y = displayRect.y;
	d.w = displayRect.w;
	d.h = displayRect.h;
	SDL_RenderCopy(renderer, realScreen.get(), &s, &d);
    drawCursor();
	SDL_RenderPresent(renderer);
}

void EngineBase::updateRect(int tempWidth, int tempHeight, Rect & rect)
{

	if (tempWidth != windowWidth || tempHeight != windowHeight)
	{
		windowWidth = tempWidth;
		windowHeight = tempHeight;
		if (_fullScreenMode != FullScreenMode::window)
		{
			rect.x = 0;
			rect.w = windowWidth;
			rect.y = 0;
			rect.h = windowHeight;
		}
		else
		{
			if ((double)windowWidth / (double)width < (double)windowHeight / (double)height)
			{
				rect.x = 0;
				rect.w = windowWidth;
				rect.y = (int)floor(((double)windowHeight - (double)height * (double)windowWidth / (double)width) / 2 + 0.5);
				rect.h = (int)floor((double)height * (double)windowWidth / (double)width + 0.5);
			}
			else
			{
				rect.y = 0;
				rect.h = windowHeight;
				rect.x = (int)floor(((double)windowWidth - (double)width * (double)windowHeight / (double)height) / 2 + 0.5);
				rect.w = (int)floor((double)width * (double)windowHeight / (double)height + 0.5);
			}
		}
	}
}

void * EngineBase::getMem(int size)
{
	if (size > 0)
	{
		return malloc((size_t)size);
	}
	return nullptr;
}

void EngineBase::freeMem(void * mem)
{
	if (mem != nullptr)
	{
		free(mem);
	}
}

int EngineBase::getLZOOutLen(int inLen)
{
	return inLen + inLen / 16 + 64 + 3;
}

int EngineBase::lzoCompress(const void * src, unsigned int srcLen, void * dst, lzo_uint * dstLen)
{
	if (lzoMem == nullptr)
	{
		lzoMem = getMem(LZO1X_1_MEM_COMPRESS);
	}
	if (lzoMem == nullptr)
	{
		return -1;
	}
	int ret = lzo1x_1_compress((const unsigned char *)src, srcLen, (unsigned char *)dst, (lzo_uint *)dstLen, lzoMem);
	freeMem(lzoMem);
	if (ret == LZO_E_OK)
	{
		return 0;
	}
	return -1;
}

int EngineBase::lzoDecompress(const void * src, unsigned int srcLen, void * dst, lzo_uint * dstLen)
{
	if (lzo1x_decompress((const unsigned char *)src, srcLen, (unsigned char *)dst, (lzo_uint *)dstLen, nullptr) == LZO_E_OK)
	{
		return 0;
	}
	return -1;
}

void EngineBase::calculateCursor(int inX, int inY, int* outX, int* outY)
{
	if (inX >= rect.x && inX < rect.x + rect.w && inY >= rect.y && inY < rect.y + rect.h)
	{
		if (outX != nullptr)
		{
			*outX = (int)round((double)(inX - rect.x) / ((double)rect.w) * (double)width );
		}
		if (outY != nullptr)
		{
			*outY = (int)round((double)(inY - rect.y) / ((double)rect.h) * (double)height );
		}
	}
}

