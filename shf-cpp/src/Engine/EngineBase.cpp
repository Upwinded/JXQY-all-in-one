#include "EngineBase.h"

EngineBase* EngineBase::this_ = NULL;

EngineBase::EngineBase()
{
	this_ = this;
	windowHeight = 0;
	windowWidth = 0;
	mousePosX = -1;
	mousePosY = -1;
}

EngineBase::~EngineBase()
{
	destroyEngineBase();
}


void EngineBase::clearMouse()
{
	for (int i = 0; i < mouseImage.imageCount; i++)
	{
		if (mouseImage.image[i].frame != NULL)
		{
			SDL_FreeCursor((SDL_Cursor *)mouseImage.image[i].frame);
			mouseImage.image[i].frame = NULL;
			freeImage(mouseImage.image[i].softFrame);
			mouseImage.image[i].softFrame = NULL;
		}
	}
	mouseImage.imageCount = 0;
	mouseImage.image.resize(mouseImage.imageCount);
	mouseImage.interval = 0;
}

void EngineBase::drawMouse()
{
	int frameIndex = -1;
	if (mouseImage.interval <= 0)
	{
		if (mouseImage.imageCount >= 1)
		{
			frameIndex = 0;
		}
	}
	else
	{
		if (mouseImage.imageCount >= 1)
		{
			frameIndex = ((int)(getTime() / mouseImage.interval)) % mouseImage.imageCount;
		}	
	}
	if (MouseImageIndex == frameIndex && hardwareCursor)
	{
		return;
	}
	if (frameIndex >= 0 && frameIndex < mouseImage.imageCount)
	{
		if (hardwareCursor)
		{
			textureMutex.lock();
			SDL_SetCursor((SDL_Cursor *)mouseImage.image[frameIndex].frame);
			textureMutex.unlock();
		}
		else if (!mouseHide)
		{
			drawImage(mouseImage.image[frameIndex].softFrame, mousePosX - mouseImage.image[frameIndex].xOffset, mousePosY - mouseImage.image[frameIndex].yOffset);
		}
		MouseImageIndex = frameIndex;
	}
	
}

void EngineBase::setMouse(MouseImage * mouse)
{
	if (mouse == NULL)
	{
		return;
	}
	clearMouse();
	mouseImage.imageCount = mouse->imageCount;
	mouseImage.image.resize(mouseImage.imageCount);
	for (int i = 0; i < mouseImage.imageCount; i++)
	{
		mouseImage.image[i].xOffset = mouse->image[i].xOffset;
		mouseImage.image[i].yOffset = mouse->image[i].yOffset;
		mouseImage.image[i].frame = mouse->image[i].frame;	
		mouseImage.image[i].softFrame = mouse->image[i].softFrame;
	}
	mouseImage.interval = mouse->interval;
}

void EngineBase::showMouse()
{
	if (hardwareCursor)
	{
		SDL_ShowCursor(1);
	}
	mouseHide = false;
}

void EngineBase::hideMouse()
{
	SDL_ShowCursor(0);
	mouseHide = true;
}

void EngineBase::drawImage(_image image, SDL_Rect * src, SDL_Rect * dst)
{
	if (image == NULL)
	{
		return;
	}
	SDL_RenderCopy(renderer, (SDL_Texture *)image, src, dst);
}

void EngineBase::drawImage(_image image, SDL_Rect * rect)
{
	if (image == NULL)
	{
		return;
	}
	textureMutex.lock();
	SDL_RenderCopy(renderer, (SDL_Texture *)image, NULL, rect);
	textureMutex.unlock();
}

bool EngineBase::pointInImage(_image image, int x, int y)
{
	SDL_Texture * from = (SDL_Texture *)image;
	int w = 0;
	int h = 0;
	getImageSize(image, &w, &h);
	if (w <= 0 || h <= 0 || x < 0 || y < 0 || x >= w || y >= h)
	{
		return false;
	}

	SDL_Texture* ts = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
	SDL_Texture* tt = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, ts);

	SDL_BlendMode bm;
	SDL_GetTextureBlendMode(from, &bm);
	SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, from, NULL, NULL);
	SDL_SetTextureBlendMode(from, bm);

	char * st = new char[w * h * 4];
	int pitch = 4 * w;
	if (st == NULL)
	{
		SDL_SetRenderTarget(renderer, tt);
		freeImage(ts);
		return false;
	}
	if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, st, pitch) != 0)
	{
		SDL_SetRenderTarget(renderer, tt);
		freeImage(ts);
		delete[] st;
		return false;
	}
	SDL_Surface * sur = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	memcpy(sur->pixels, st, pitch * h);
	SDL_SetRenderTarget(renderer, tt);
	freeImage(ts);
	SDL_FreeSurface(sur);
	bool result = false;
	if (st != NULL)
	{
		if (*(st + y * pitch + x * 4 + 3) != 0)
		{
			result = true;
		}
		delete[] st;
	}

	return result;
}

unsigned int EngineBase::initTime()
{
	time.beginTime = SDL_GetTicks();
	time.paused = false;
	return time.beginTime;
}

unsigned int EngineBase::getTime()
{
	if (time.paused)
	{
		return time.pauseBeginTime - time.beginTime;
	}
	else
	{
		return SDL_GetTicks() - time.beginTime;
	}
}

void EngineBase::setTimePaused(bool paused)
{
	if (paused == time.paused)
	{
		return;
	}
	if (paused)
	{
		time.paused = true;
		time.pauseBeginTime = SDL_GetTicks();
	}
	else
	{
		time.paused = false;
		time.beginTime += SDL_GetTicks() - time.pauseBeginTime;
	}
}

unsigned int EngineBase::initTime(Time * t)
{
	unsigned int now = 0;
	t->beginTime = getTime();
	t->paused = false;
	return t->beginTime;
}

unsigned int EngineBase::getTime(Time * t)
{
	if (t->paused)
	{
		return t->pauseBeginTime - t->beginTime;
	}
	else
	{
		return getTime() - t->beginTime;
	}
}

void EngineBase::setTime(Time * t, unsigned int time)
{
	unsigned int tm = getTime(t);
	if (tm > time)
	{
		t->beginTime += tm - time;
	}
	else
	{
		t->beginTime -= time - tm;
	}
}

void EngineBase::setTimePaused(Time * t, bool paused)
{
	if (paused == t->paused)
	{
		return;
	}
	if (paused)
	{
		t->paused = true;
		t->pauseBeginTime = getTime();
	}
	else
	{
		t->paused = false;
		t->beginTime += getTime() - t->pauseBeginTime;
	}
}

void EngineBase::delay(unsigned int t)
{
	SDL_Delay(t);
}

int EngineBase::getFPS()
{
	return FPS;
}

_image EngineBase::createNewImageFromImage(_image image)
{
	if (image == NULL)
	{
		return NULL;
	}
	SDL_Texture* from = (SDL_Texture *)image;
	int w, h; 
	getImageSize(image, &w, &h);
	if ((w <= 0) || (h <= 0))
	{
		return NULL;
	}

	SDL_Texture* ts = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
	SDL_Texture* tt = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, ts);
	SDL_BlendMode bm;
	SDL_GetTextureBlendMode(from, &bm);
	SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, from, NULL, NULL);
	SDL_SetTextureBlendMode(from, bm);	

	SDL_Texture* to = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
	void * pixels = getMem(w * h * 4);
	int pitch = w * 4;
	SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, pitch);
	SDL_UpdateTexture(to, NULL, pixels, pitch);
	SDL_SetRenderTarget(renderer, tt);
	freeMem(pixels);
	freeImage(ts);
	SDL_SetTextureBlendMode(to, SDL_BLENDMODE_BLEND);
	return _image(to);
}

_image EngineBase::loadImageFromMem(char * data, int size)
{
	if (data == NULL || size <= 0)
	{
		return NULL;
	}
	textureMutex.lock();
	_image ret = (_image)IMG_LoadTexture_RW(renderer, SDL_RWFromMem(data, size), 1);
	textureMutex.unlock();
	return ret;
}

_image EngineBase::loadImageFromFile(const std::string & fileName)
{
	char * data;
	int size;
	if (!File::readFile(fileName, &data, &size))
	{
		printf("Image File Readed Error\n");
		return NULL;
	}
	if (data == NULL || size <= 0)
	{
		printf("Image File Readed Error\n");
		return NULL;
	}
	void * result = loadImageFromMem(data, size);
	delete[] data;
	return result;
}

int EngineBase::saveImageToFile(_image image, int w, int h, const std::string & fileName)
{
	if (image == NULL)
	{
		return -1;
	}
	char * data = NULL;
	int len = saveImageToMem(image, w, h, &data);
	if (len > 0 && data != NULL)
	{
		File::writeFile(fileName, data, len);
		delete[] data;
		return len;
	}
	else
	{
		return -1;
	}
}

int EngineBase::saveImageToFile(_image image, const std::string & fileName)
{
	if (image == NULL)
	{
		return -1;
	}
	int w, h;
	getImageSize(image, &w, &h);
	return saveImageToFile(image, w, h, fileName);
}

int EngineBase::saveImageToMem(_image image, int w, int h, char ** data)
{
	if (image == NULL || w <= 0 || h <= 0 || data == NULL)
	{
		return -1;
	}
	if (w <= 0 || h <= 0 || image == NULL)
	{
		return -1;
	}

	SDL_Texture * from = (SDL_Texture *)image;
	SDL_Texture* ts = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
	SDL_Texture* tt = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, ts);

	SDL_BlendMode bm;
	SDL_GetTextureBlendMode(from, &bm);
	SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, from, NULL, NULL);
	SDL_SetTextureBlendMode(from, bm);

	char * st = new char[w * h * 4];
	int pitch = 4 * w;
	if (st == NULL)
	{
		printf("allocing memory error\n");
		SDL_SetRenderTarget(renderer, tt);
		freeImage(ts);
		return -1;
	}
	if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, st, pitch) != 0)
	{
		printf("reading pixels error\n");
		SDL_SetRenderTarget(renderer, tt);
		freeImage(ts);
		delete[] st;
		return -1;
	}
	SDL_Surface * sur = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	memcpy(sur->pixels, st, pitch * h);

	SDL_SetRenderTarget(renderer, tt);
	freeImage(ts);
	delete[] st;

	int rwSize = w * h * 4 + 122;
	char * rwData = new char[rwSize];
	
	SDL_RWops * bmp = SDL_RWFromMem(NULL, rwSize);
	bmp->seek(bmp, 0, RW_SEEK_SET);
	if (bmp == NULL)
	{
		SDL_FreeRW(bmp);
		SDL_FreeSurface(sur);
		delete[] rwData;
		return -1;
	}
	SDL_SaveBMP_RW(sur, bmp, 0);

	int size = (int)bmp->seek(bmp, 0, RW_SEEK_END);
	if (size >= rwSize)
	{
		*data = new char[rwSize];
		bmp->seek(bmp, 0, 0);
		bmp->read(bmp, *data, rwSize, 1);
		SDL_FreeSurface(sur);
		SDL_FreeRW(bmp);
		delete[] rwData;
		bmp = NULL;
		size = rwSize;
	}
	return size;

}

int EngineBase::saveImageToMem(_image image, char ** data)
{
	if (image == NULL)
	{
		return -1;
	}
	int w, h;
	getImageSize(image, &w, &h);
	return saveImageToMem(image, w, h, data);
}

_image EngineBase::loadSurfaceFromMem(char * data, int size)
{
	if (data == NULL || size <= 0)
	{
		return NULL;
	}
	_image image = (_image)IMG_Load_RW(SDL_RWFromMem(data, size), 1);
	return image;
}

_cursor EngineBase::loadCursorFromMem(char * data, int size, int x, int y)
{
	SDL_Surface * s = (SDL_Surface *)loadSurfaceFromMem(data, size);
	SDL_Cursor * cursor = SDL_CreateColorCursor((SDL_Surface *)s, x, y);
	SDL_FreeSurface(s);
	return (_cursor)cursor;
}

void EngineBase::drawImage(_image image, int x, int y)
{
	textureMutex.lock();
	if (image == NULL)
	{
		textureMutex.unlock();
		return;
	}
	int w, h;
	SDL_QueryTexture((SDL_Texture *)image, NULL, NULL, &w, &h);
	SDL_Rect s, d;
	s.x = 0;
	s.y = 0;
	s.w = w;
	s.h = h;
	d.x = x;
	d.y = y;
	d.w = w;
	d.h = h;
	SDL_RenderCopy(renderer, (SDL_Texture *)image, NULL, &d);
	textureMutex.unlock();
}

void EngineBase::drawImage(_image image, Rect * rect)
{
	drawImage(image, NULL, rect);
}

void EngineBase::drawImage(_image image, Rect * src, Rect * dst)
{
	if (image == NULL)
	{
		return;
	}
	SDL_Rect s;
	SDL_Rect d;
	SDL_Rect * ps = NULL;
	SDL_Rect * pd = NULL;
	if (src != NULL)
	{
		s.x = src->x;
		s.y = src->y;
		s.w = src->w;
		s.h = src->h;
		ps = &s;
	}
	if (dst != NULL)
	{
		d.x = dst->x;
		d.y = dst->y;
		d.w = dst->w;
		d.h = dst->h;
		pd = &d;
	}
	textureMutex.lock();
	SDL_RenderCopy(renderer, (SDL_Texture *)image, ps, pd);
	textureMutex.unlock();
}

void EngineBase::freeImage(_image image)
{
	if (image)
	{
		textureMutex.lock();
		SDL_DestroyTexture((SDL_Texture *)image);
		textureMutex.unlock();
		image = NULL;
	}
}

_image EngineBase::createMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	textureMutex.lock();
	SDL_Texture * tt = SDL_GetRenderTarget(renderer);
	SDL_Texture * t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 1, 1);
	if (t == NULL)
	{
		textureMutex.unlock();
		return NULL;
	}
	SDL_Texture * t2 = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
	if (t2 == NULL)
	{
		textureMutex.unlock();
		freeImage(t);
		return NULL;
	}
	SDL_SetRenderTarget(renderer, t);
	SDL_SetRenderDrawColor(renderer, r, g, b, 255);
	SDL_RenderClear(renderer);
	void * pixels = getMem(4);
	int pitch = 4;
	SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, pitch);
	SDL_UpdateTexture(t2, NULL, pixels, pitch);
	SDL_SetRenderTarget(renderer, tt);
	freeMem(pixels);
	SDL_SetTextureBlendMode(t2, SDL_BLENDMODE_BLEND);
	setImageAlpha(t2, a);
	textureMutex.unlock();
	freeImage(t);
	return (_image)t2;
}

_image EngineBase::createLumMask()
{
	textureMutex.lock();
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
				unsigned char a = unsigned char((0.5 - distance) * LUM_MASK_MAX_ALPHA);
				c[i][j] = (a << 24) | (0xFFFFFF);
			}
		}
	}
	memcpy(s->pixels, c, LUM_MASK_HEIGHT * LUM_MASK_WIDTH * 4);
	delete[] c;
	SDL_Texture * t = SDL_CreateTextureFromSurface(renderer, s);
	SDL_FreeSurface(s);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
	textureMutex.unlock();
	return (_image)t;
}

void EngineBase::setImageAlpha(_image image, unsigned char a)
{
	SDL_SetTextureBlendMode((SDL_Texture *)image, SDL_BLENDMODE_BLEND);
	SDL_SetTextureAlphaMod((SDL_Texture *)image, a);	
}

void EngineBase::setImageColorMode(_image image, unsigned char r, unsigned char g, unsigned char b)
{
	SDL_SetTextureColorMod((SDL_Texture *)image, r, g, b);
}

void EngineBase::drawImageWithColor(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
	if (image == NULL)
	{
		return;
	}
	SDL_SetTextureColorMod((SDL_Texture *)image, r, g, b);
	drawImage(image, x, y);
	SDL_SetTextureColorMod((SDL_Texture *)image, 255, 255, 255);
}

int EngineBase::getImageSize(_image image, int * w, int * h)
{
	if (image == NULL)
	{
		return -1;
	}
	return SDL_QueryTexture((SDL_Texture *)image, NULL, NULL, w, h);
}

_image EngineBase::beginDrawTalk(int w, int h)
{
	if (w <= 0 || h <= 0)
	{
		return nullptr;
	}
	originalTex = (_image)SDL_GetRenderTarget(renderer);
	SDL_Texture * t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
	setImageAlpha(t, 255);
	SDL_SetRenderTarget(renderer, t);
	SDL_Surface * ts = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
	Uint32 color = 0xFFFFFF;
	memcpy(ts->pixels, &color, 4);
	SDL_Texture * t2 = SDL_CreateTextureFromSurface(renderer, ts);
	SDL_FreeSurface(ts);
	SDL_SetTextureBlendMode(t2, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, t2, nullptr, nullptr);
	SDL_DestroyTexture(t2);
	return (_image)t;
}

_image EngineBase::endDrawTalk()
{
	SDL_Texture * t = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, (SDL_Texture *)originalTex);
	SDL_Texture * newT = (SDL_Texture *)createNewImageFromImage((_image)t);
	SDL_DestroyTexture(t);
	return (_image)newT;
}

_image EngineBase::createBMP16(int w, int h, char * data, int size)
{
	if (w <= 0 || h <= 0 || data == NULL || size <= 0 || w * h * 2 > size)
	{
		return NULL;
	}
	SDL_Surface * sur = SDL_CreateRGBSurfaceWithFormat(0, w, h, 16, BMP16Format);
	if (sur == NULL)
	{
		return NULL;
	}
	if (w % 2 == 0)
	{
		memcpy(sur->pixels, data, w * 2 * h);
	}
	else
	{
		for (int i = 0; i < h; i++)
		{
			if (i < w)
			{
				memcpy(((char *)sur->pixels) + w * i * 2, data + i * w * 2 + w * 2 - i * 2, i * 2);
				memcpy(((char *)sur->pixels) + w * i * 2 + i * 2, data + i * w * 2, w * 2 - i * 2);
			}
			else
			{
				memcpy(((char *)sur->pixels) + w * i * 2, data + i * w * 2, w * 2);
			}
		}
	}
	SDL_Texture * t = SDL_CreateTextureFromSurface(renderer, sur);
	SDL_FreeSurface(sur);
	return t;
}

_image EngineBase::beginSaveScreen()
{
	originalTex = (_image)SDL_GetRenderTarget(renderer);
	SDL_Texture * t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width, height);
	SDL_SetRenderTarget(renderer, t);
	return (_image)t;
}

_image EngineBase::endSaveScreen()
{
	SDL_Texture * t = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, (SDL_Texture *)originalTex);
	return (_image)t;
}

int EngineBase::saveImageToBMP16(_image image, int w, int h, char ** s)
{
	if (w <= 0 || h <= 0 || s == NULL || image == NULL)
	{
		return -1;
	}
	
	SDL_Texture * from = (SDL_Texture *)image;
	SDL_Texture* ts = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
	SDL_Texture* tt = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, ts);	

	SDL_BlendMode bm;
	SDL_GetTextureBlendMode(from, &bm);
	SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, from, NULL, NULL);
	SDL_SetTextureBlendMode(from, bm);

	*s = new char[w * h * 2];
	char * st = new char[w * h * 2];
	int pitch = 2 * w;

	if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGB565, st, pitch) != 0)
	{
		delete[] st;
		delete[] * s;
		*s = NULL;
		return -1;
	}
	memcpy(*s, st, w * 2 * h);	
	
	delete[] st;
	SDL_SetRenderTarget(renderer, tt);

	freeImage(ts);

	return w * h * 2;
}

_image EngineBase::createRaindrop()
{
	textureMutex.lock();
	const int w = 2;
	const int h = 115;

	SDL_Surface * s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (s == NULL)
	{
		textureMutex.unlock();
		return NULL;
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
	SDL_Texture * t = SDL_CreateTextureFromSurface(renderer, s);
	SDL_FreeSurface(s);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	textureMutex.unlock();
	return _image(t);
}

_image EngineBase::createSnowflake()
{
	textureMutex.lock();
	SDL_Surface * s = SDL_CreateRGBSurfaceWithFormat(0, 3, 3, 32, SDL_PIXELFORMAT_ARGB8888);
	if (s == NULL)
	{
		textureMutex.unlock();
		return NULL;
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
	SDL_Texture * t = SDL_CreateTextureFromSurface(renderer, s);
	SDL_FreeSurface(s);
	setImageAlpha(t, 0xB0);
	textureMutex.unlock();
	return _image(t);
}

void EngineBase::loadLogo()
{
	if (logo != NULL)
	{
		freeImage(logo);
		logo = NULL;
	}

#ifdef USE_LOGO_RESOURCE
	HRSRC hRsrc = FindResource(NULL, TEXT("PngImage_1"), RT_RCDATA);
	if (hRsrc == NULL)
	{
		logo = loadImageFromFile("Logo.png");
		printf("Logo Loaded From File\n");
		return;
	}
	printf("Logo Loaded From Resource\n");

	unsigned int size = SizeofResource(NULL, hRsrc);
	if (size == 0)
	{
		logo = loadImageFromFile("Logo.png");
		return;
	}
	HGLOBAL hGlobal = LoadResource(NULL, hRsrc);
	if (hGlobal == NULL)
	{
		logo = loadImageFromFile("Logo.png");
		return;
	}
	LPVOID pBuffer = LockResource(hGlobal);
	if (pBuffer == NULL)
	{
		logo = loadImageFromFile("Logo.png");
		return;
	}
	logo = loadImageFromMem((char *)pBuffer, size);
	UnlockResource(hGlobal);
	FreeResource(hGlobal);
#else
	logo = loadImageFromFile("Logo.png");
#endif // USE_LOGO_RESOURCE
}

void EngineBase::fadeInLogo()
{
	if (logo == NULL)
	{
		return;
	}
	int w, h;
	if (getImageSize(logo, &w, &h) == 0)
	{
		unsigned char r, g, b;
		r = (clLogoBG & 0xFF0000) >> 16;
		g = (clLogoBG & 0xFF00) >> 8;
		b = clLogoBG & 0xFF;
		setScreenMask(r, g, b, 255);
		Time t;
		initTime(&t);
		while (getTime(&t) < 1000)
		{
			frameBegin();
			drawScreenMask();
			unsigned char a = (unsigned char)(((double)getTime(&t)) / (double)1000 * (double)255);
			setImageAlpha(logo, a);
			drawImage(logo, (width - w) / 2, (height - h) / 2);
			frameEnd();
		}
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
	if (logo == NULL)
	{
		return;
	}
	int w, h;
	if (getImageSize(logo, &w, &h) == 0)
	{
		Time t;
		initTime(&t);
		while (getTime(&t) < 1000)
		{
			frameBegin();
			drawScreenMask();
			unsigned char a = (unsigned char)(((double)1000 - (double)getTime(&t)) / (double)1000 * (double)255);
			setImageAlpha(logo, a);
			drawImage(logo, (width - w) / 2, (height - h) / 2);
			frameEnd();
		}
		frameBegin();
		drawScreenMask();
		frameEnd();
	}
}

void EngineBase::drawImageWithMask(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == NULL)
	{
		return;
	}
	_image mask = createMask(r, g, b, a);
	drawImageWithMask(image, x, y, mask);
	freeImage(mask);
}

void EngineBase::drawImageWithMask(_image image, int x, int y, _image mask)
{
	if (image == NULL || mask == NULL)
	{
		return;
	}
	int w, h;
	SDL_QueryTexture((SDL_Texture *)image, NULL, NULL, &w, &h);
	SDL_Texture * tt = SDL_GetRenderTarget(renderer);
	drawImage(image, x, y);
	SDL_Rect sdlrect;
	sdlrect.x = x;
	sdlrect.y = y;
	sdlrect.w = w;
	sdlrect.h = h;
	SDL_RenderCopy(renderer, (SDL_Texture *)mask, NULL, &sdlrect);
}

void EngineBase::drawImageWithMask(_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == NULL)
	{
		return;
	}
	_image mask = createMask(r, g, b, a);
	drawImageWithMask(image, src, dst, mask);
	freeImage(mask);
}

void EngineBase::drawImageWithMask(_image image, Rect * src, Rect * dst, _image mask)
{
	if (image == NULL || mask == NULL)
	{
		return;
	}
	SDL_Rect d;
	SDL_Rect * pd = NULL;
	if (dst != NULL)
	{
		d.x = dst->x;
		d.y = dst->y;
		d.w = dst->w;
		d.h = dst->h;
		pd = &d;
	}
	drawImage(image, src, dst);
	SDL_RenderCopy(renderer, (SDL_Texture *)mask, NULL, pd);
}

void EngineBase::drawImageWithMaskEx(_image image, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == NULL)
	{
		return;
	}
	_image mask = createMask(r, g, b, a);
	drawImageWithMaskEx(image, x, y, mask);
	freeImage(mask);
}

void EngineBase::drawImageWithMaskEx(_image image, int x, int y, _image mask)
{
	if (image == NULL || mask == NULL)
	{
		return;
	}
	int w, h;
	getImageSize(image, &w, &h);
	if (w <= 0 || h <= 0)
	{
		return;
	}
	Rect dst;
	dst.x = x;
	dst.w = w;
	dst.y = y;
	dst.h = h;
	drawImageWithMaskEx(image, NULL, &dst, mask);
}

void EngineBase::drawImageWithMaskEx(_image image, Rect * src, Rect * dst, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (image == NULL)
	{
		return;
	}
	_image mask = createMask(r, g, b, a);
	drawImageWithMaskEx(image, src, dst, mask);
	freeImage(mask);
}

void EngineBase::drawImageWithMaskEx(_image image, Rect * src, Rect * dst, _image mask)
{
	if (image == NULL || mask == NULL)
	{
		return;
	}
	int w, h;
	SDL_QueryTexture((SDL_Texture *)image, NULL, NULL, &w, &h);
	SDL_Texture * t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
	SDL_Texture * tt = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, t);
	SDL_SetTextureBlendMode((SDL_Texture *)image, SDL_BLENDMODE_NONE);
	drawMask(image);
	SDL_SetTextureBlendMode((SDL_Texture *)image, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode((SDL_Texture *)mask, SDL_BLENDMODE_ADD);
	drawMask(mask);
	SDL_SetRenderTarget(renderer, tt);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	drawImage(t, src, dst);
	freeImage(t);
}

void EngineBase::setScreenMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (screenMask == NULL)
	{
		return;
	}
	*(unsigned int *)((SDL_Surface *)screenMask)->pixels = 0xFF << 24 | r << 16 | g << 8| b;
	SDL_SetSurfaceAlphaMod((SDL_Surface *)screenMask, a);	
}

void EngineBase::drawScreenMask()
{
	if (screenMask == NULL)
	{
		return;
	}
	SDL_Texture * t = SDL_CreateTextureFromSurface(renderer, (SDL_Surface *)screenMask);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	drawMask(t);
	freeImage(t);
}

void EngineBase::drawMask(_image mask)
{
	drawMask(mask, NULL);
}

void EngineBase::drawMask(_image mask, Rect* dst)
{
	if (mask == NULL)
	{
		return;
	}
	SDL_Rect d;
	SDL_Rect * pd = NULL;
	if (dst != NULL)
	{
		d.x = dst->x;
		d.y = dst->y;
		d.w = dst->w;
		d.h = dst->h;
		pd = &d;
	}
	SDL_RenderCopy(renderer, (SDL_Texture *)mask, NULL, pd);
}

void EngineBase::handleEvent()
{
	clearEventList();
	SDL_Event e;
	setTimePaused(true);

	while (SDL_PollEvent(&e))
	{
		switch (e.type)
		{
			case SDL_QUIT:
			{
				eventList.event.push_back({ QUIT , 0 });
				break;
			}
			case SDL_KEYDOWN: case SDL_KEYUP:
			{				
				eventList.event.push_back({ (EventType)e.type , e.key.keysym.scancode });
				break;
			}
			case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
			{
				eventList.event.push_back({ (EventType)e.type , e.button.button });
				break;
			}
			default:
			{
				break;
			}			
		}
	}
	setTimePaused(false);
}

void EngineBase::copyEvent(AEvent* s, AEvent* d)
{
	d->eventType = s->eventType;
	d->eventData = s->eventData;
}

void EngineBase::clearEventList()
{
	eventList.event.resize(0);
}

int EngineBase::getEventCount()
{
	return (int)eventList.event.size();
}

int EngineBase::getEvent(AEvent * event)
{
	if (eventList.event.size() == 0)
	{
		return 0;
	}
	if (event != NULL)
	{
		copyEvent(&eventList.event[0], event);
	}	
	for (size_t i = 0; i < eventList.event.size() - 1; i++)
	{
		copyEvent(&eventList.event[i + 1], &eventList.event[i]);
	}
	eventList.event.erase(eventList.event.begin());
	return (int)eventList.event.size() + 1;
}

void EngineBase::pushEvent(AEvent * event)
{
	if (event == NULL)
	{
		return;
	}
	eventList.event.push_back(*event);
}

//需要自己释放
int EngineBase::readEventList(EventList * eList)
{
	if (!eventList.event.size())
	{
		return 0;
	}

	eList->event.resize(eventList.event.size());
	for (size_t i = 0; i < eList->event.size(); i++)
	{
		copyEvent(&(eventList.event[i]), &(eList->event[i]));
	}
	return eList->event.size();
}

bool EngineBase::getKeyPress(KeyCode key)
{
	return (SDL_GetKeyboardState(NULL)[key] != 0);
}

bool EngineBase::getMousePress(MouseButtonCode button)
{
	return ((SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(button)) != 0);
}

void EngineBase::getMouse(int * x, int * y)
{
	if (x != NULL)
	{
		*x = mouseX;
	}
	if (y != NULL)
	{
		*y = mouseY;
	}
}

void EngineBase::resetEvent()
{
	handleEvent();
	clearEventList();
}

void EngineBase::setMouseHardWare(bool isHardware)
{
	hardwareCursor = isHardware;
	if (hardwareCursor && !mouseHide)
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
	if (fontData != NULL)
	{
		SDL_FreeRW(fontData);
		fontData = NULL;
	}
	fontData = SDL_RWFromFile(fontName.c_str(), "r+");
	if (!fontData)
	{
		printf("there is no fontData\n");
	}
}

void EngineBase::drawSolidUnicodeText(const std::wstring & text, int x, int y, int size, unsigned int color)
{
	_image t = createUnicodeText(text, size, color);
	SDL_SetTextureBlendMode((SDL_Texture *)t, SDL_BLENDMODE_NONE);
	drawImage(t, x, y);
	freeImage(t);
}

void EngineBase::setFontFromMem(void * data, int size)
{
	if (data == NULL || size <= 0)
	{
		return;
	}
	if (fontData != NULL)
	{
		SDL_FreeRW(fontData);
		fontData = NULL;
	}
	if (fontBuffer != NULL)
	{
		delete[] fontBuffer;
		fontBuffer = NULL;
	}
	fontData = SDL_RWFromMem(data, size);
	fontBuffer = (char *)data;
}

_image EngineBase::createUnicodeText(const std::wstring& text, int size, unsigned int color)
{
	textureMutex.lock();
	TTF_Font * _font = NULL;
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
		textureMutex.unlock();
		return nullptr;
	}
	SDL_Color c;
	c.b = (color & 0xFF);
	c.g = (color & 0xFF00) >> 8;
	c.r = (color & 0xFF0000) >> 16;
	c.a = (color & 0xFF000000) >> 24;

	auto text_s = TTF_RenderUNICODE_Blended(_font, (Uint16 *)text.c_str(), c);
	auto text_t = SDL_CreateTextureFromSurface(renderer, text_s);
	setImageAlpha(text_t, c.a);

	SDL_FreeSurface(text_s);
	TTF_CloseFont(_font);
	textureMutex.unlock();
	return (_image)text_t;
}

_image EngineBase::createText(const std::string& text, int size, unsigned int color)
{
	textureMutex.lock();
	TTF_Font * _font = NULL;
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
		textureMutex.unlock();
		return nullptr; 
	}

	SDL_Color c;
	c.b = (color & 0xFF);
	c.g = (color & 0xFF00) >> 8;
	c.r = (color & 0xFF0000) >> 16;	
	c.a = (color & 0xFF000000) >> 24;

	auto text_s = TTF_RenderUTF8_Blended(_font, text.c_str(), c);
	auto text_t = SDL_CreateTextureFromSurface(renderer, text_s);
	setImageAlpha(text_t, c.a);

	SDL_FreeSurface(text_s);
	TTF_CloseFont(_font);
	textureMutex.unlock();
	return (_image)text_t;
}

void EngineBase::drawUnicodeText(const std::wstring& text, int x, int y, int size, unsigned int color)
{
	_image t = createUnicodeText(text, size, color);
	drawImage(t, x, y);
	freeImage(t);

}

void EngineBase::drawText(const std::string & text, int x, int y, int size, unsigned int color)
{
	_image t = createText(text, size, color);
	drawImage(t, x, y);	
	freeImage(t);
}

InitErrorType EngineBase::initEngineBase(const std::string & windowCaption, int wWidth, int wHeight, bool isFullScreen)
{
	width = wWidth;
	height = wHeight;
	if (initSDL(windowCaption, wWidth, wHeight, isFullScreen) != 0)
	{
		printf("Init SDL Error!\n");
		return sdlError;
	}
	
	if (initSoundSystem() != 0)
	{
		printf("Init Sound Error!\n");
		return soundError;
	}
	if (initVideo() != 0)
	{
		printf("Init Video Error!\n");
		return videoError;
	}
	
	if (lzo_init() != LZO_E_OK)
	{
		printf("Init miniLZO Error!\n");
		return LZOError;
	}

	fadeOutLogo();

	resetEvent();

	if (hardwareCursor)
	{
		showMouse();
	}
	return initOK;
}

void EngineBase::destroyEngineBase()
{
	if (lzoMem != NULL)
	{
		freeMem(lzoMem);
		lzoMem = NULL;
	}

	freeImage(logo);
	
	destroyVideo();

	destroySoundSystem();

	destroySDL();
}

bool EngineBase::setFullScreen(bool full)
{
	if (full == fullScreen)
	{
		return fullScreen;
	}
	fullScreen = full;
	unsigned int flags = SDL_GetWindowFlags(window);
	if (fullScreen)
	{
		flags |= SDL_WINDOW_FULLSCREEN;
	}
	else
	{
		flags ^= SDL_WINDOW_FULLSCREEN;
	}
	if (fullScreen)
	{
		if (canChangeDisplayMode)
		{
			SDL_DisplayMode dm;
			SDL_GetDisplayMode(0, 0, &dm);
			dm.w = width;
			dm.h = height;
			SDL_SetWindowSize(window, dm.w, dm.h);
			SDL_SetWindowFullscreen(window, flags);
		}
		else
		{
			SDL_DisplayMode dm;
			SDL_GetDisplayMode(0, 0, &dm);
			SDL_SetWindowSize(window, dm.w, dm.h);
			SDL_SetWindowFullscreen(window, flags);
		}	
	}
	else
	{
		SDL_DisplayMode dm;
		SDL_GetDisplayMode(0, 0, &dm);
		SDL_SetWindowFullscreen(window, flags);
		if (width > dm.w || height > dm.h)
		{
			SDL_SetWindowSize(window, dm.w, dm.h);
			SDL_SetWindowPosition(window, 0, 0);
		}
		else
		{
			SDL_SetWindowSize(window, width, height);
			SDL_SetWindowPosition(window, (dm.w - width) / 2, (dm.h - height) / 2);
		}	
	}
	updateState();
	flags = SDL_GetWindowFlags(window);
	return fullScreen = ((flags & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN);
}

bool EngineBase::setDisplayMode(bool dm)
{
	canChangeDisplayMode = dm;
	return dm;
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
		bool renderRealScreen = (t == realScreen);
		SDL_Texture * newScreen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width, height);
		SDL_SetRenderTarget(renderer, newScreen);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
		SDL_RenderClear(renderer);
		drawImage((_image)realScreen, (Rect *)NULL);
		if (!renderRealScreen)
		{
			SDL_SetRenderTarget(renderer, t);
		}
		freeImage(realScreen);
		realScreen = newScreen;
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

InitErrorType EngineBase::initSDL(const std::string & windowCaption, int wWidth, int wHeight, bool isFullScreen)
{
	if (SDL_Init(SDL_INIT_EVERYTHING))
	{
		return sdlError;
	}
	SDL_ShowCursor(0);
	fullScreen = isFullScreen;
	Uint32 flags = SDL_WINDOW_RESIZABLE;

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

	SDL_DisplayMode dm;
	dm.w = wWidth;
	dm.h = wHeight;

	if (fullScreen)
	{
		flags |= SDL_WINDOW_FULLSCREEN;
		if (!canChangeDisplayMode)
		{
			SDL_GetCurrentDisplayMode(0, &dm);
		}
	}
	else
	{
		flags |= SDL_WINDOW_HIDDEN;
	}

	window = SDL_CreateWindow(windowCaption.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, dm.w, dm.h, flags);

	if (fullScreen)
	{
		SDL_SetWindowSize(window, dm.w, dm.h);
	}

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);	

	realScreen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, wWidth, wHeight);
	
	screenMask = SDL_CreateRGBSurface(0, 1, 1, 32, 0, 0, 0, 0);

	initTime();
	
	updateState();

	loadLogo();

	SDL_ShowWindow(window);
	SDL_RaiseWindow(window);

	fadeInLogo();

	TTF_Init();

	clearMouse();

	initTime();

	return initOK;
}

void EngineBase::destroySDL()
{
	printf("Begin destroy SDL\n");
	destroyMouse();
	if (fontBuffer != NULL)
	{
		delete[] fontBuffer;
		fontBuffer = NULL;
	}
	if (fontData != NULL)
	{
		SDL_FreeRW(fontData);
		fontData = NULL;
	}
	if (screenMask)
	{
		SDL_FreeSurface((SDL_Surface *)screenMask);
		screenMask = NULL;
	}
	if (realScreen)
	{
		freeImage(realScreen);
		realScreen = NULL;
	}
	if (renderer)
	{
		SDL_DestroyRenderer(renderer);
		renderer = NULL;
	}
	if (window)
	{
		SDL_DestroyWindow(window);
		window = NULL;
	}
	SDL_Quit();
	printf("Destroy SDL done!\n");
}

void EngineBase::destroyMouse()
{
	clearMouse();
}

void EngineBase::updateState()
{
	updateRect();	
	updateMouse();
}

int EngineBase::initSoundSystem()
{
	if (FMOD_System_Create(&soundSystem) != 0)
	{
		return soundError;
	}
	if (FMOD_System_Init(soundSystem, maxChannel, FMOD_INIT_NORMAL, NULL) != 0)
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
	printf("Begin destroy sound system\n");
	soundMutex.lock();
	for (size_t i = 0; i < soundList.sound.size(); i++)
	{
		//stopMusic(soundList.sound[i].c);
		if (soundList.sound[i].m != NULL)
		{
			freeMusic(soundList.sound[i].m);
		}	
	}
	soundList.sound.resize(0);
	soundMutex.unlock();
	//FMOD_System_Update(soundSystem);
	if (soundSystem != NULL)
	{		
		FMOD_System_Close(soundSystem);
		FMOD_System_Release(soundSystem);
		soundSystem = NULL;
	}
	printf("Destroy sound system done!\n");
}

void EngineBase::updateSoundSystem()
{
	FMOD_System_Update(soundSystem);
}

FMOD_RESULT F_CALLBACK EngineBase::autoReleaseSound(FMOD_CHANNELCONTROL * chanControl, FMOD_CHANNELCONTROL_TYPE controlType, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType, void * commandData1, void * commandData2)
{
	if (controlType == FMOD_CHANNELCONTROL_CHANNEL && callbackType == FMOD_CHANNELCONTROL_CALLBACK_END)
	{
		_channel channel = (_channel)chanControl;
		if (channel == NULL)
		{
			return FMOD_RESULT_FORCEINT;
		}
		int index = -1;
		this_->soundMutex.lock();
		if (this_->soundList.sound.size() == 0)
		{
			this_->soundMutex.unlock();
			return FMOD_OK;
		}
		for (size_t i = 0; i < this_->soundList.sound.size(); i++)
		{
			if (this_->soundList.sound[i].c == channel)
			{
				index = i;
				this_->freeMusic(this_->soundList.sound[i].m);
			}
		}		
		if (index < 0)
		{
			this_->soundMutex.unlock();
			return FMOD_OK;
		}

		for (size_t i = index; i < this_->soundList.sound.size() - 1; i++)
		{
			this_->soundList.sound[i].c = this_->soundList.sound[i + 1].c;
			this_->soundList.sound[i].m = this_->soundList.sound[i + 1].m;
		}
		this_->soundList.sound.resize(this_->soundList.sound.size() - 1);
		this_->soundMutex.unlock();
	}
	return FMOD_OK;
}

_music EngineBase::createMusic(char * data, int size, bool loop, bool music3d, unsigned char priority)
{
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

	if (FMOD_System_CreateSound(soundSystem, data, mode, &exinfo, &sound) != 0)
	{
		//printf("Create Sound Error!\n");
		return NULL;
	}
	if (priority != 128)
	{
		float f;
		FMOD_Sound_GetDefaults(sound, &f, NULL);
		FMOD_Sound_SetDefaults(sound, f, priority);
	}
	if (music3d)
	{
		if (FMOD_Sound_Set3DMinMaxDistance(sound, 0.5f, 5000.0f) != 0)
		{
			printf("Set3DMinMaxDistance Error!\n");
		}
	}
	return (_music)sound;
}

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
	if (tempSystem == NULL)
	{
		tempSystem = soundSystem;
	}
	int ret = FMOD_System_CreateSound(tempSystem, data, mode, &exinfo, &sound);
	if (ret != 0)
	{
		//printf("Create Sound Error! %d\n", ret);
		return NULL;
	}
	if (priority != 128)
	{
		float f;
		FMOD_Sound_GetDefaults(sound, &f, NULL);
		FMOD_Sound_SetDefaults(sound, f, priority);
	}
	if (music3d)
	{
		if (FMOD_Sound_Set3DMinMaxDistance(sound, 0.5f, 5000.0f) != 0)
		{
			printf("Set3DMinMaxDistance Error!\n");
		}
	}
	return (_music)sound;
}

void EngineBase::freeMusic(_music music)
{
	FMOD_Sound_Release((FMOD_SOUND *)music);
}

_channel EngineBase::playMusic(_music music, float volume)
{
	if (music == NULL)
	{
		return NULL;
	}
	FMOD_CHANNEL * channel = NULL;
	if (FMOD_System_PlaySound(soundSystem, (FMOD_SOUND *)music, NULL, true, &channel) != 0)
	{
		printf("Play Sound Error!\n");
	}
	setMusicVolume((_channel)channel, volume);
	resumeMusic(channel);
	return (_channel)channel;
}
_channel EngineBase::playMusic(_music music, float x, float y, float volume)
{
	if (music == NULL)
	{
		return NULL;
	}
	FMOD_CHANNEL * channel;
	FMOD_System_PlaySound(soundSystem, (FMOD_SOUND *)music, NULL, true, &channel);
	setMusicPosition((_channel)channel, x, y);
	setMusicVolume((_channel)channel, volume);
	resumeMusic(channel);
	return (_channel)channel;
}

void EngineBase::stopMusic(_channel channel)
{
	FMOD_Channel_Stop((FMOD_CHANNEL *)channel);
}

void EngineBase::pauseMusic(_channel channel)
{
	FMOD_Channel_SetPaused((FMOD_CHANNEL *)channel, true);
}

void EngineBase::resumeMusic(_channel channel)
{
	FMOD_Channel_SetPaused((FMOD_CHANNEL *)channel, false);
}

void EngineBase::setMusicPosition(_channel channel, float x, float y)
{
	FMOD_VECTOR vector;
	vector.x = x;
	vector.y = 0;
	vector.z = y;
	FMOD_VECTOR vel = { 0.0f, 0.0f, 0.0f };
	FMOD_Channel_Set3DAttributes((FMOD_CHANNEL *)channel, &vector, &vel, NULL);
}

void EngineBase::setMusicVolume(_channel channel, float volume)
{
	FMOD_Channel_SetVolume((FMOD_CHANNEL *)channel, volume);
}

bool EngineBase::getMusicPlaying(_channel channel)
{
	bool playing = false;
	if (FMOD_Channel_IsPlaying((FMOD_CHANNEL *)channel, (FMOD_BOOL *)&playing) == 0)
	{
		return playing;
	}
	return false;
}

bool EngineBase::soundAutoRelease(_music music, _channel channel)
{
	this_->soundMutex.lock();
	for (size_t i = 0; i < soundList.sound.size(); i++)
	{
		if (soundList.sound[i].c == channel || soundList.sound[i].m == music)
		{
			this_->soundMutex.unlock();
			return false;
		}
	}
	soundList.sound.resize(soundList.sound.size() + 1);
	soundList.sound[soundList.sound.size() - 1].c = channel;
	soundList.sound[soundList.sound.size() - 1].m = music;
	FMOD_Channel_SetCallback((FMOD_CHANNEL *)(soundList.sound[soundList.sound.size() - 1].c), autoReleaseSound);
	this_->soundMutex.unlock();
	return true;
}

int EngineBase::initVideo()
{
	av_register_all();
	avformat_network_init();
	clearVideoList();
	return 0;
}

void EngineBase::destroyVideo()
{
	printf("Begin destroy video\n");
	clearVideoList();
	printf("Destroy video done!\n");
	return;
}

void EngineBase::freeMediaStream(MediaStream * mediaStream)
{
	if (mediaStream == NULL)
	{
		return;
	}
	if (mediaStream->frame != NULL)
	{
		av_frame_free(&mediaStream->frame);
	}
	if (mediaStream->codecCtx != NULL)
	{
		if (mediaStream->codec != NULL)
		{
			avcodec_close(mediaStream->codecCtx);
			mediaStream->codec = NULL;
		}		
		avcodec_free_context(&mediaStream->codecCtx);
	}
	if (mediaStream->formatCtx != NULL)
	{
		avformat_close_input(&mediaStream->formatCtx);
	}
}

int EngineBase::openVideoFile(_video video)
{
	video_ v = (video_)video;
	int result = -1;
	if (v == NULL)
	{
		return result;
	}
	if (!File::fileExist(v->fileName))
	{
		return result;
	}
	
	setMediaStream(&v->videoStream, v->fileName, AVMEDIA_TYPE_VIDEO);
	setMediaStream(&v->audioStream, v->fileName, AVMEDIA_TYPE_AUDIO);

	if (v->audioStream.exists)
	{
		v->totalTime = v->audioStream.totalTime;
		result = 1;
	}
	else if (v->videoStream.exists)
	{
		v->totalTime = v->videoStream.totalTime;
		result = 1;
	}
	else
	{
		v->totalTime = 0;
	}
	return result;
}

void EngineBase::setMediaStream(MediaStream * mediaStream, std::string& fileName, AVMediaType mediaType)
{
	if (mediaStream == NULL)
	{
		return;
	}
	if (!File::fileExist(fileName))
	{
		return;
	}
	if (avformat_open_input(&mediaStream->formatCtx, fileName.c_str(), NULL, NULL) == 0)
	{
		if (avformat_find_stream_info(mediaStream->formatCtx, nullptr) >= 0)
		{
			for (size_t i = 0; i < mediaStream->formatCtx->nb_streams; i++)
			{
				if (mediaStream->formatCtx->streams[i]->codecpar->codec_type == mediaType)
				{
					mediaStream->exists = true;
					mediaStream->stream = mediaStream->formatCtx->streams[i];
					mediaStream->codecCtx = avcodec_alloc_context3(NULL);
					avcodec_parameters_to_context(mediaStream->codecCtx, mediaStream->stream->codecpar);
					if (mediaStream->stream->r_frame_rate.den)
					{
						mediaStream->timePerFrame = 1e3 / av_q2d(mediaStream->stream->r_frame_rate);
					}
					mediaStream->timeBasePacket = 1e3 * av_q2d(mediaStream->stream->time_base);
					mediaStream->totalTime = mediaStream->formatCtx->duration * 1e3 / AV_TIME_BASE;
					mediaStream->startTime = mediaStream->formatCtx->start_time * 1e3 / AV_TIME_BASE;
					mediaStream->index = i;
					mediaStream->codec = avcodec_find_decoder(mediaStream->codecCtx->codec_id);
					avcodec_open2(mediaStream->codecCtx, mediaStream->codec, nullptr);
					break;
				}
			}			
		}
	}	
}

double EngineBase::getVideoTime(_video video)
{
	video_ v = (video_)video;
	if (v == NULL || v->cg == NULL)
	{
		return 0.0;
	}
	if (v->time.paused)
	{
		return v->time.pauseBeginTime - v->time.beginTime;
	}
	else
	{
		unsigned long long t = 0;
		if (FMOD_ChannelGroup_GetDSPClock(v->cg, 0, &t) == 0)
		{
			return ((double)t) / v->soundRate - v->time.beginTime;
		}
	}
	return 0.0;
}

double EngineBase::initVideoTime(_video video)
{
	video_ v = (video_)video;
	if (v == NULL || v->cg == NULL)
	{
		return 0.0;
	}
	unsigned long long t = 0;
	if (FMOD_ChannelGroup_GetDSPClock(v->cg, 0, &t) == 0)
	{
		v->time.beginTime = ((double)t) / v->soundRate;
		v->time.pauseBeginTime = 0.0;
		v->time.paused = false;
		return v->time.beginTime;
	}
	return 0.0;
}

void EngineBase::setVideoTimePaused(_video video, bool paused)
{
	video_ v = (video_)video;
	if (v == NULL || v->cg == NULL)
	{
		return;
	}
	if (v->time.paused == paused)
	{
		return;
	}
	unsigned long long t = 0;
	if (FMOD_ChannelGroup_GetDSPClock(v->cg, 0, &t) == 0)
	{
		double now = ((double)t) / v->soundRate;
		if (paused)
		{
			v->time.pauseBeginTime = now;
			v->time.paused = true;
		}
		else
		{
			v->time.beginTime += now - v->time.pauseBeginTime;
			v->time.paused = false;
		}
	}
}

double EngineBase::setVideoTime(_video video, double time)
{
	video_ v = (video_)video;
	if (v == NULL || v->cg == NULL)
	{
		return 0.0;
	}
	v->time.beginTime += getVideoTime(v) - time;
	return getVideoTime(v);
}

double EngineBase::getVideoSoundRate(_video video)
{
	video_ v = (video_)video;
	int outputRate = 0;
	if (v == NULL || v->soundSystem == NULL)
	{
		FMOD_System_GetSoftwareFormat(soundSystem, &outputRate, 0, 0);
	}
	else
	{
		FMOD_System_GetSoftwareFormat(v->soundSystem, &outputRate, 0, 0);
	}
	return ((double)outputRate) / 1000.0;
}

void EngineBase::decodeNextAudio(_video video)
{
	video_ v = (video_)video;
	if (v == NULL || !v->audioStream.exists)
	{
		return;
	}
	int ret = 0;
	while (ret == 0)
	{
		v->audioStream.packet = av_packet_alloc();
		bool haveFrame = (av_read_frame(v->audioStream.formatCtx, v->audioStream.packet) >= 0);
		if (!haveFrame)
		{
			v->audioStream.decodeEnd = true;
			av_packet_free(&v->audioStream.packet);
			break;
		}
		if (v->audioStream.packet->stream_index == v->audioStream.index)
		{
			//循环处理多次才能解到一帧的情况
			int gotframe = 0;
			int gotsize = 0;
			int state = 0;

			int64_t pts = 0;
			int64_t dts = 0;
			if (avcodec_send_packet(v->audioStream.codecCtx, v->audioStream.packet) == 0)
			{			
				if (!v->audioStream.setTS)
				{
					pts = v->audioStream.packet->pts;
					dts = v->audioStream.packet->dts;
					v->audioStream.setTS = true;
				}
				while (avcodec_receive_frame(v->audioStream.codecCtx, v->audioStream.frame) == 0)
				{
					v->audioStream.setTS = false;
					ret = 1;
					AVFrame * f = v->audioStream.frame;
					int data_length_ = convert(v->audioStream.codecCtx, v->audioStream.frame, AV_SAMPLE_FMT_S16, v->audioStream.codecCtx->sample_rate, v->audioStream.codecCtx->channels, (uint8_t *)v->buffer);
					if (data_length_ > 0)
					{
						if (v->soundSystem == NULL)
						{
							if (FMOD_System_Create(&v->soundSystem) == 0 &&
								FMOD_System_SetSoftwareFormat(v->soundSystem, v->audioStream.codecCtx->sample_rate, FMOD_SPEAKERMODE_DEFAULT, 0) == 0 &&
								FMOD_System_Init(v->soundSystem, 64, FMOD_INIT_NORMAL, NULL) == 0)
							{
								FMOD_ChannelGroup_Release(v->cg);
								FMOD_System_CreateChannelGroup(v->soundSystem, NULL, &v->cg);
								v->soundRate = getVideoSoundRate(v);
								initVideoTime(v);
								setVideoTimePaused(v, true);
							}
						}
						FMOD_SYSTEM * tempSystem = v->soundSystem;
						if (tempSystem == NULL)
						{
							tempSystem = soundSystem;
						}
						_music m = createVideoRAW(tempSystem, v->buffer, data_length_, false, false, FMOD_SOUND_FORMAT_PCM16, v->audioStream.codecCtx->channels, v->audioStream.codecCtx->sample_rate, 1);
						v->soundTime.resize(v->soundTime.size() + 1);
						//v->soundTime[v->soundTime.size() - 1] = ((double)pts * v->audioStream.timeBasePacket);
						v->soundTime[v->soundTime.size() - 1] = v->soundDelay;
						double addTime = ((double)data_length_) / 2.0 / (((double)v->audioStream.codecCtx->sample_rate) / 1000.0) / ((double)v->audioStream.codecCtx->channels);
						v->soundDelay += addTime;
						v->videoSounds.resize(v->videoSounds.size() + 1);
						v->videoSounds[v->videoSounds.size() - 1] = m;
						v->videoSoundChannels.resize(v->videoSoundChannels.size() + 1);
						FMOD_CHANNEL * c;
						if (FMOD_System_PlaySound(tempSystem, (FMOD_SOUND *)m, v->cg, true, &c) == 0)
						{
							v->videoSoundChannels[v->videoSoundChannels.size() - 1] = c;
							setMusicVolume(c, v->videoVolume);
							FMOD_Channel_SetCallback(c, audioCallback);
							if (v->running)
							{
								if ((v->soundTime[v->soundTime.size() - 1] - getVideoTime(v)) < 0)
								{
									setVideoTime(v, (v->soundTime[v->soundTime.size() - 1]));
								}
								unsigned long long clock_start = 0;
								FMOD_Channel_GetDSPClock(c, 0, &clock_start);
								clock_start += (unsigned long long)((v->soundTime[v->soundTime.size() - 1] - getVideoTime(v)) * v->soundRate + 0.5);
								FMOD_Channel_SetDelay(c, clock_start, 0, true);
								if (!v->time.paused)
								{
									resumeMusic(c);
								}
							}
						}
						else
						{
							v->videoSoundChannels[v->videoSoundChannels.size() - 1] = NULL;
						}
					}
				}
			}	
			
		}
		av_packet_free(&v->audioStream.packet);
	}
}

void EngineBase::decodeNextVideo(_video video)
{
	video_ v = (video_)video;
	if (v == NULL || !v->videoStream.exists)
	{
		return;
	}
	int ret = 0;
	while (ret == 0)
	{
		v->videoStream.packet = av_packet_alloc();
		bool haveFrame = (av_read_frame(v->videoStream.formatCtx, v->videoStream.packet) >= 0);
		if (!haveFrame)
		{
			v->videoStream.decodeEnd = true;
			break;
		}
		if (v->videoStream.packet->stream_index == v->videoStream.index)
		{
			int gotframe = 0;
			int gotsize = 0;
			int state = 0;

			int64_t pts = 0;
			int64_t dts = 0;

			if (avcodec_send_packet(v->videoStream.codecCtx, v->videoStream.packet) == 0)
			{
				if (!v->videoStream.setTS)
				{
					pts = v->videoStream.packet->pts;
					dts = v->videoStream.packet->dts;
					v->videoStream.setTS = true;
				}
				while (avcodec_receive_frame(v->videoStream.codecCtx, v->videoStream.frame) == 0)
				{
					v->videoStream.setTS = false;
					ret = 2;
					if (v->swsContext == NULL)
					{
						v->swsContext = sws_getContext(v->videoStream.codecCtx->width, v->videoStream.codecCtx->height, v->videoStream.codecCtx->pix_fmt, v->videoStream.codecCtx->width, v->videoStream.codecCtx->height, AV_PIX_FMT_YUV420P/*AV_PIX_FMT_RGB24*/, SWS_BICUBIC, NULL, NULL, NULL);
						v->b = av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P/*AV_PIX_FMT_RGB24*/, v->videoStream.codecCtx->width, v->videoStream.codecCtx->height, 1));
						av_image_fill_arrays(v->sFrame->data, v->sFrame->linesize, (const uint8_t *)v->b, AV_PIX_FMT_YUV420P/*AV_PIX_FMT_RGB24*/, v->videoStream.codecCtx->width, v->videoStream.codecCtx->height, 1);
					}
					AVFrame * f = v->videoStream.frame;
					SDL_Texture * tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, v->videoStream.codecCtx->width, v->videoStream.codecCtx->height);
					sws_scale(v->swsContext, (const uint8_t* const*)f->data, f->linesize, 0, v->videoStream.codecCtx->height, (uint8_t* const*)v->sFrame->data, v->sFrame->linesize);
					SDL_UpdateYUVTexture(tex, NULL, v->sFrame->data[0], v->sFrame->linesize[0], v->sFrame->data[1], v->sFrame->linesize[1], v->sFrame->data[2], v->sFrame->linesize[2]);
					v->videoTime.push_back((double)((dts > 0 ? dts : pts)) * v->videoStream.timeBasePacket);
					v->image.push_back((_image)tex);
				}
			}
				
		}
		av_packet_free(&v->videoStream.packet);
	}
}

void EngineBase::checkVideoDecodeEnd(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	if ((v->audioStream.exists && !v->audioStream.decodeEnd) || (v->videoStream.exists && !v->videoStream.decodeEnd))
	{
		v->decodeEnd = false;
	}
	else
	{
		v->decodeEnd = true;
	}
}

bool EngineBase::getVideoStopped(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return true;
	}
	return v->stopped;
}

void EngineBase::pauseAllVideo()
{
	for (size_t i = 0; i < videoList.size(); i++)
	{
		if (videoList[i] == NULL)
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
		if (videoList[i] == NULL)
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
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	for (size_t i = 0; i < v->videoSoundChannels.size(); i++)
	{
		stopMusic(v->videoSoundChannels[i]);
	}
	v->videoSoundChannels.resize(0);
	for (size_t i = 0; i < v->videoSounds.size(); i++)
	{
		freeMusic(v->videoSounds[i]);
	}
	v->videoSounds.resize(0);
	v->soundTime.resize(0);

	for (size_t i = 0; i < v->image.size(); i++)
	{
		freeImage(v->image[i]);
	}
	v->image.resize(0);
	v->videoTime.resize(0);
}

void EngineBase::rearrangeVideoFrame(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	for (int i = 0; i < (int)v->image.size(); i++)
	{
		for (int j = 0; j < int(v->image.size()) - i - 1; j--)
		{
			if (v->videoTime[j] > v->videoTime[j + 1])
			{
				double t = v->videoTime[j];
				v->videoTime[j] = v->videoTime[j + 1];
				v->videoTime[j + 1] = t;
			}
		}
	}
}

void EngineBase::resetVideo(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	initVideoTime(v);
	setVideoTimePaused(v, true);
	clearVideo(v);

	if (v->videoStream.exists)
	{
		avcodec_flush_buffers(v->videoStream.codecCtx);
		av_seek_frame(v->videoStream.formatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
		v->videoStream.decodeEnd = false;
		v->videoStream.setTS = false;
	}
	if (v->audioStream.exists)
	{
		avcodec_flush_buffers(v->audioStream.codecCtx);
		av_seek_frame(v->audioStream.formatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
		v->audioStream.decodeEnd = false;
		v->audioStream.setTS = false;
	}

	v->soundDelay = 0.0;
	v->soundRate = getVideoSoundRate(v);
	v->pausedBeforePause = false;
	v->decodeEnd = false;
	v->stopped = false;
	if (v->running)
	{
		v->running = false;
		runVideo(v);
	}
}

void EngineBase::setVideoLoop(_video video, int loop)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	v->loop = loop;
}

FMOD_RESULT F_CALLBACK EngineBase::audioCallback(FMOD_CHANNELCONTROL * chanControl, FMOD_CHANNELCONTROL_TYPE controlType, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType, void * commandData1, void * commandData2)
{
	if (controlType == FMOD_CHANNELCONTROL_CHANNEL && callbackType == FMOD_CHANNELCONTROL_CALLBACK_END)
	{
		_channel channel = (_channel)chanControl;
		if (channel == NULL)
		{
			return FMOD_RESULT_FORCEINT;
		}
		int index = -1;
		int index2 = 0;
		for (size_t i = 0; i < this_->videoList.size(); i++)
		{
			if (this_->videoList[i]->videoSoundChannels.size() > 0)
			{
				bool find = false;
				for (size_t j = 0; j < this_->videoList[i]->videoSoundChannels.size(); j++)
				{
					if (this_->videoList[i]->videoSoundChannels[j] == channel)
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
				this_->freeMusic(this_->videoList[index]->videoSounds[i]);
			}
			for (int i = 0; i < ((int)this_->videoList[index]->videoSounds.size()) - index2 - 1; i++)
			{
				this_->videoList[index]->videoSounds[i] = this_->videoList[index]->videoSounds[i + index2 + 1];
				this_->videoList[index]->videoSoundChannels[i] = this_->videoList[index]->videoSoundChannels[i + index2 + 1];
				this_->videoList[index]->soundTime[i] = this_->videoList[index]->soundTime[i + index2 + 1];
			}
			this_->videoList[index]->videoSounds.resize(this_->videoList[index]->videoSounds.size() - index2 - 1);
			this_->videoList[index]->videoSoundChannels.resize(this_->videoList[index]->videoSoundChannels.size() - index2 - 1);
			this_->videoList[index]->soundTime.resize(this_->videoList[index]->soundTime.size() - index2 - 1);
		}

	}
	return FMOD_OK;
}

int EngineBase::convert(AVCodecContext * codecCtx, AVFrame * frame, int out_sample_format, int out_sample_rate, int out_channels, uint8_t * out_buf)
{
	SwrContext* swr_ctx = NULL;
	int data_size = 0;
	int ret = 0;
	int64_t src_ch_layout = codecCtx->channel_layout;
	int64_t dst_ch_layout = AV_CH_LAYOUT_STEREO;
	int dst_nb_channels = 0;
	int dst_linesize = 0;
	int src_nb_samples = 0;
	int dst_nb_samples = 0;
	int max_dst_nb_samples = 0;
	uint8_t** dst_data = NULL;
	int resampled_data_size = 0;

	swr_ctx = swr_alloc();
	if (!swr_ctx)
	{
		printf("swr_alloc error \n");
		return -1;
	}

	src_ch_layout = (codecCtx->channels ==
		av_get_channel_layout_nb_channels(codecCtx->channel_layout)) ?
		codecCtx->channel_layout : av_get_default_channel_layout(codecCtx->channels);

	//这里的设置很粗糙，最好详细处理
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

	if (src_ch_layout <= 0)
	{
		printf("src_ch_layout error \n");
		return -1;
	}

	src_nb_samples = frame->nb_samples;
	if (src_nb_samples <= 0)
	{
		printf("src_nb_samples error \n");
		return -1;
	}

	av_opt_set_int(swr_ctx, "in_channel_layout", src_ch_layout, 0);
	av_opt_set_int(swr_ctx, "in_sample_rate", codecCtx->sample_rate, 0);
	av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", codecCtx->sample_fmt, 0);

	av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);
	av_opt_set_int(swr_ctx, "out_sample_rate", out_sample_rate, 0);
	av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", (AVSampleFormat)out_sample_format, 0);

	if ((ret = swr_init(swr_ctx)) < 0)
	{
		printf("Failed to initialize the resampling context\n");
		return -1;
	}

	max_dst_nb_samples = dst_nb_samples = (int)av_rescale_rnd(src_nb_samples, out_sample_rate, codecCtx->sample_rate, AV_ROUND_UP);
	if (max_dst_nb_samples <= 0)
	{
		printf("av_rescale_rnd error \n");
		return -1;
	}

	dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);
	ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels, dst_nb_samples, (AVSampleFormat)out_sample_format, 0);
	if (ret < 0)
	{
		printf("av_samples_alloc_array_and_samples error \n");
		return -1;
	}

	dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(swr_ctx, codecCtx->sample_rate) + src_nb_samples, out_sample_rate, codecCtx->sample_rate, AV_ROUND_UP);
	if (dst_nb_samples <= 0)
	{
		printf("av_rescale_rnd error \n");
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
			printf("swr_convert error \n");
			return -1;
		}

		resampled_data_size = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels, ret, (AVSampleFormat)out_sample_format, 1);
		if (resampled_data_size < 0)
		{
			printf("av_samples_get_buffer_size error \n");
			return -1;
		}
	}
	else
	{
		printf("swr_ctx null error \n");
		return -1;
	}

	memcpy(out_buf, dst_data[0], resampled_data_size);
	swr_close(swr_ctx);

	if (dst_data)
	{
		av_freep(&dst_data[0]);
	}
	av_freep(&dst_data);
	dst_data = NULL;

	if (swr_ctx)
	{
		swr_free(&swr_ctx);
	}
	return resampled_data_size;
}

void EngineBase::clearVideoList()
{
	videoList.resize(0);
}

void EngineBase::addVideoToList(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	videoList.resize(videoList.size() + 1);
	videoList[videoList.size() - 1] = v;
}

void EngineBase::deleteVideoFromList(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	int index = -1;
	for (int i = 0; i < (int)videoList.size(); i++)
	{
		if (videoList[i] == v)
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
	for (int i = index; i < (int)videoList.size() - 1; i++)
	{
		videoList[i] = videoList[i + 1];
	}
	videoList.resize(videoList.size() - 1);
}

_video EngineBase::createNewVideo(const std::string& fileName)
{
	printf("Open video %s\n", fileName.c_str());
	if (!File::fileExist(fileName))
	{
		printf("Video:%s not exists\n", fileName.c_str());
		return NULL;
	}
	VideoStruct * v = new VideoStruct;
	v->image.resize(0);
	v->videoTime.resize(0);
	v->soundTime.resize(0);
	v->videoSounds.resize(0);
	v->videoSoundChannels.resize(0);
	v->videoStream.formatCtx = avformat_alloc_context();
	v->audioStream.formatCtx = avformat_alloc_context();
	v->videoStream.frame = av_frame_alloc();
	v->audioStream.frame = av_frame_alloc();
	v->sFrame = av_frame_alloc();
 	v->buffer = (char *)av_mallocz(videoConvertSize);
	FMOD_System_CreateChannelGroup(soundSystem, NULL, &v->cg);
	v->soundDelay = 0;
	v->soundRate = getVideoSoundRate(v);
	initVideoTime(v);
	setVideoTimePaused(v, true);
	v->fileName = fileName;
	setVideoRect((_video)v, NULL);
	v->decodeEnd = false;
	v->videoVolume = 1;
	if (openVideoFile(v) < 0)
	{
		printf("Open video:%s error\n", fileName.c_str());
		freeVideo(v);
		return NULL;
	}

	addVideoToList(v);

	return (_video)v;
}

void EngineBase::setVideoRect(_video video, Rect * rect)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	if (rect == NULL)
	{
		v->fullScreen = true;
	}
	else
	{
		v->fullScreen = false;
		v->rect.x = rect->x;
		v->rect.y = rect->y;
		v->rect.w = rect->w;
		v->rect.h = rect->h;
	}
}

void EngineBase::freeVideo(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	deleteVideoFromList(v);
	stopVideo(v);
	if (v->buffer != NULL)
	{
		av_free(v->buffer);
		v->buffer = NULL;
	}
	if (v->sFrame != NULL)
	{
		av_frame_free(&v->sFrame);
		v->sFrame = NULL;
	}
	if (v->swsContext != NULL)
	{
		sws_freeContext(v->swsContext);
		v->swsContext = NULL;
	}
	if (v->b != NULL)
	{
		av_free(v->b);
		v->b = NULL;
	}
	freeMediaStream(&v->audioStream);
	freeMediaStream(&v->videoStream);
	if (v->cg != NULL)
	{
		FMOD_ChannelGroup_Release(v->cg);
		v->cg = NULL;
	}
	if (v->soundSystem != NULL)
	{
		FMOD_System_Release(v->soundSystem);
		v->soundSystem = NULL;
	}
	delete v;
}

void EngineBase::runVideo(_video video)
{	
	video_ v = (video_)video;
	if (v == NULL || v->running)
	{
		return;
	}
	v->stopped = false;
	initVideoTime(v);
	setVideoTimePaused(v, true);
	tryDecodeVideo(v);
	v->running = true;

	if (v->audioStream.exists)
	{
		if (v->videoSounds.size() > 0)
		{
			for (size_t i = 0; i < v->videoSoundChannels.size(); i++)
			{
				unsigned long long clock_start = 0;
				FMOD_Channel_GetDSPClock((FMOD_CHANNEL *)v->videoSoundChannels[i], 0, &clock_start);
				clock_start += (unsigned long long)((v->soundTime[i]) * v->soundRate + 0.5);
				FMOD_Channel_SetDelay((FMOD_CHANNEL *)v->videoSoundChannels[i], clock_start, 0, true);
				resumeMusic(v->videoSoundChannels[i]);
			}
		}	
	}
	initVideoTime(v);
}

bool EngineBase::updateVideo(_video video)
{	
	video_ v = (video_)video;
	if (v == NULL)
	{
		return false;
	}
	if (v->soundSystem != NULL)
	{
		FMOD_System_Update(v->soundSystem);
	}
	else
	{
		updateSoundSystem();
	}
	tryDecodeVideo(v);
	if (v->stopped)
	{
		if (v->loop > 0)
		{
			v->loop -= 1;
			resetVideo(v);
		}
		else if (v->loop < 0)
		{
			resetVideo(v);
		}
	}	
	return true;
}

void EngineBase::tryDecodeVideo(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	if (v->stopped)
	{
		return;
	}
	if (v->running && v->time.paused)
	{
		return;
	}
	if (getVideoTime(v) > v->totalTime)
	{
		v->stopped = true;
		clearVideo(v);
	}
	if (v->audioStream.exists)
	{
		if (v->videoStream.exists)
		{
			while ((!v->audioStream.decodeEnd) && ((v->videoSounds.size() < (unsigned int)2) || (v->soundTime.size() > 0 && (v->soundTime[v->soundTime.size() - 1] < getVideoTime(v) + 100))))
			{
				decodeNextAudio(v);
				checkVideoDecodeEnd(v);
			}
			while ((!v->videoStream.decodeEnd) && ((v->image.size() < (unsigned int)2) || (v->videoTime.size() > 0 && (v->videoTime[v->videoTime.size() - 1] < getVideoTime(v) + 100))))
			{
				decodeNextVideo(v);
				checkVideoDecodeEnd(v);
			}
		}
		else
		{
			while ((!v->audioStream.decodeEnd) && ((v->videoSounds.size() < (unsigned int)2) || (v->soundTime.size() > 0 && (v->soundTime[v->soundTime.size() - 1] < getVideoTime(v) + 100))))
			{
				decodeNextAudio(v);
				checkVideoDecodeEnd(v);
			}
		}
	}
	else if (v->videoStream.exists)
	{
		while ((!v->videoStream.decodeEnd) && ((v->image.size() < (unsigned int)2) || (v->videoTime.size() > 0 && (v->videoTime[v->videoTime.size() - 1] < getVideoTime(v) + 100))))
		{
			decodeNextVideo(v);
			checkVideoDecodeEnd(v);
		}
	}
}

void EngineBase::drawVideoFrame(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	SDL_Rect* rect;
	if (v->fullScreen)
	{
		rect = NULL;
	}
	else
	{
		rect = &v->rect;
	}
	//rearrangeVideoFrame(v);
	double t = getVideoTime(v);
	if (v->image.size() == 0)
	{
		_image image = createMask(0, 0, 0, 255);
		drawImage(image, rect);
		freeImage(image);
	}
	else if (v->image.size() == 1)
	{
		drawImage(v->image[0], rect);
	}
	else if (v->image.size() > 1)
	{
		int index = 0;
		for (size_t i = 0; i < v->image.size(); i++)
		{
			if ((int)t >= v->videoTime[i])
			{
				index = i;
			}
		}

		drawImage(v->image[index], rect);

		for (int i = 0; i < index; i++)
		{
			freeImage(v->image[i]);
		}

		for (int i = 0; i < ((int)v->image.size()) - index; i++)
		{
			v->image[i] = v->image[i + index];
			v->videoTime[i] = v->videoTime[i + index];
		}

		v->image.resize(v->image.size() - index);
		v->videoTime.resize(v->videoTime.size() - index);		
	}
}

bool EngineBase::onVideoFrame(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return false;
	}
	if (updateVideo(v))
	{
		drawVideoFrame(v);
		return true;
	}
	return false;
}

void EngineBase::pauseVideo(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	if (v->audioStream.exists)
	{
		for (size_t i = 0; i < v->videoSoundChannels.size(); i++)
		{
			pauseMusic(v->videoSoundChannels[i]);
		}	
	}
	setVideoTimePaused(v, true);
}

void EngineBase::resumeVideo(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	setVideoTimePaused(v, false);
	if (v->audioStream.exists)
	{		
		for (size_t i = 0; i < v->videoSoundChannels.size(); i++)
		{
			if (v->soundTime[i] > getVideoTime(v))
			{
				unsigned long long clock_start = 0;
				FMOD_Channel_GetDSPClock((FMOD_CHANNEL *)v->videoSoundChannels[i], 0, &clock_start);
				clock_start += (unsigned long long)((v->soundTime[i] - getVideoTime(v)) * v->soundRate);
				FMOD_Channel_SetDelay((FMOD_CHANNEL *)v->videoSoundChannels[i], clock_start, 0, true);
			}
			resumeMusic(v->videoSoundChannels[i]);
		}
	}
	
}

void EngineBase::stopVideo(_video video)
{
	video_ v = (video_)video;
	if (v == NULL)
	{
		return;
	}
	v->running = false;
	resetVideo(v);
}

void EngineBase::frameBegin()
{	
	updateState();
	clearScreen();
	handleEvent();
}

void EngineBase::frameEnd()
{
	updateSoundSystem();
	displayScreen();
	countFPS();
}

void EngineBase::clearScreen()
{
	textureMutex.lock();
	SDL_SetRenderTarget(renderer, realScreen);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);
	textureMutex.unlock();
}

void EngineBase::displayScreen()
{	
	textureMutex.lock();
	unsigned int engineBaseBackCol = clBG;
	SDL_SetRenderTarget(renderer, NULL);
	SDL_SetRenderDrawColor(renderer, (engineBaseBackCol & 0xFF0000) >> 16, (engineBaseBackCol & 0xFF00) >> 8, engineBaseBackCol & 0xFF, 0);
	SDL_RenderClear(renderer);
	SDL_Rect s, d;
	s.x = 0;
	s.y = 0;
	s.h = height;
	s.w = width;
	d.x = rect.x;
	d.y = rect.y;
	d.w = rect.w;
	d.h = rect.h;
	SDL_RenderCopy(renderer, realScreen, &s, &d);
	textureMutex.unlock();
	drawMouse();
	textureMutex.lock();
	SDL_RenderPresent(renderer);
	textureMutex.unlock();
}

void EngineBase::updateRect()
{
	int tempWidth = 0;
	int tempHeight = 0;
	SDL_GetWindowSize(window, &tempWidth, &tempHeight);
	if (tempWidth != windowWidth || tempHeight != windowHeight)
	{
		windowWidth = tempWidth;
		windowHeight = tempHeight;
		if (fullScreen)
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
	if (mem != NULL)
	{
		free(mem);
	}
}

int EngineBase::getLZOOutLen(int inLen)
{
	return inLen + inLen / 16 + 64 + 3;
}

int EngineBase::lzoCompress(const void * src, unsigned int srcLen, void * dst, unsigned int * dstLen)
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

int EngineBase::lzoDecompress(const void * src, unsigned int srcLen, void * dst, unsigned int * dstLen)
{
	if (lzo1x_decompress((const unsigned char *)src, srcLen, (unsigned char *)dst, (lzo_uint *)dstLen, NULL) == LZO_E_OK)
	{
		return 0;
	}
	return -1;
}

void EngineBase::updateMouse()
{
	SDL_GetMouseState(&mousePosX, &mousePosY);
	if (mousePosX >= rect.x && mousePosX < rect.x + rect.w  && mousePosY >= rect.y && mousePosY < rect.y + rect.h)
	{
		mouseX = (int)floor((double)(mousePosX - rect.x) / ((double)rect.w) * (double)width + 0.5);
		mouseY = (int)floor((double)(mousePosY - rect.y) / ((double)rect.h) * (double)height + 0.5);
	}
}
