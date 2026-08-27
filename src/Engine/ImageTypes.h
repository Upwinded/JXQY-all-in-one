#pragma once

#include <memory>

#include <SDL3/SDL_render.h>

using Image_t = SDL_Texture;
using _image = Image_t*;
using _shared_image = std::shared_ptr<Image_t>;
using Vertex = SDL_Vertex;

struct IMPImage;
using _shared_imp = std::shared_ptr<IMPImage>;

#define make_shared_imp() std::make_shared<IMPImage>()

namespace ImageThreadSafety
{
void flushPendingTextureDestructions();
void destroyTexture(Image_t* texture);
}

inline _shared_image makeSharedImage(Image_t* image)
{
	return _shared_image(image, ImageThreadSafety::destroyTexture);
}

#define make_shared_image(a) makeSharedImage(a)
#define make_safe_shared_image(a) makeSharedImage(a)
