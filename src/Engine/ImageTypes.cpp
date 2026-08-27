#include "ImageTypes.h"

#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

namespace
{
std::mutex& pendingTextureMutex()
{
	// Engine is a process-global object and drains this queue from its
	// destructor. Keep the synchronization storage alive through static
	// teardown.
	static std::mutex* mutex = new std::mutex();
	return *mutex;
}

std::vector<Image_t*>& pendingTextures()
{
	static std::vector<Image_t*>* textures = new std::vector<Image_t*>();
	return *textures;
}

void SDLCALL destroyTexturesOnMainThreadCallback(void*)
{
	ImageThreadSafety::flushPendingTextureDestructions();
}
}

void ImageThreadSafety::flushPendingTextureDestructions()
{
	std::vector<Image_t*> textures;
	{
		std::lock_guard<std::mutex> locker(pendingTextureMutex());
		textures.swap(pendingTextures());
	}
	for (Image_t* texture : textures)
	{
		SDL_DestroyTexture(texture);
	}
}

void ImageThreadSafety::destroyTexture(Image_t* texture)
{
	if (texture == nullptr)
	{
		return;
	}
	if (SDL_IsMainThread())
	{
		SDL_DestroyTexture(texture);
		return;
	}
	try
	{
		std::lock_guard<std::mutex> locker(pendingTextureMutex());
		pendingTextures().push_back(texture);
	}
	catch (...)
	{
		SDL_Log("Unable to queue texture destruction on the SDL main thread");
		return;
	}
	// Renderer-owned textures must be destroyed on SDL's main thread. Keeping
	// our own queue also lets shutdown drain callbacks before the renderer dies.
	if (!SDL_RunOnMainThread(
		destroyTexturesOnMainThreadCallback, nullptr, false))
	{
		SDL_Log(
			"Unable to schedule texture destruction on the SDL main thread: %s",
			SDL_GetError());
	}
}
