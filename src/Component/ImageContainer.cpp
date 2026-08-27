#include "ImageContainer.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"

namespace
{
bool registeredImageContainer = []
{
	ComponentRegistry::getInstance().registerType(
		"ImageContainer",
		[]() -> std::shared_ptr<BaseComponent>
		{
			return std::make_shared<ImageContainer>();
		});
	return true;
}();
}

ImageContainer::ImageContainer()
{
	setPriority(epImage);
	name = "ImageContainer";
	elementType = etImageContainer;
	coverMouse = false;
}

ImageContainer::~ImageContainer()
{
	freeResource();
}

void ImageContainer::freeResource()
{
	impImage = nullptr;
	cachedCropImage = nullptr;
	cachedCropRect = { 0, 0, 0, 0 };
	cachedCropValid = false;
	frameIndex = -1;
	removeAllChild();
}

void ImageContainer::initFromIni(INIReader& ini)
{
	freeResource();

	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	name = ini.Get("Init", "Name", name);
	stretch = ini.GetBoolean("Init", "Stretch", stretch);
	keepAspect = ini.GetBoolean("Init", "KeepAspect", false);
	fadeMirroredBars = ini.GetBoolean(
		"Init", "FadeMirroredBars", false);
	cropContent = ini.GetBoolean("Init", "CropContent", false);
	cropBlack = ini.GetBoolean("Init", "CropBlack", false);
	frameIndex = ini.GetInteger("Init", "Frame", -1);
	std::string impName = ini.Get("Init", "Image", "");
	if (impName.empty())
	{
		impName = ini.Get("Init", "Bitmap", "");
	}
	impImage = loadRes(impName);
}

void ImageContainer::onDraw()
{
	drawImagetoRect(rect, stretch);
}

bool ImageContainer::drawImagetoRect(
	Rect destinationRect,
	bool drawStretch)
{
	_shared_image image = frameIndex >= 0
		? IMP::loadImage(impImage, frameIndex)
		: IMP::loadImageForTime(impImage, getTime());
	if (image == nullptr)
	{
		return false;
	}

	if (!drawStretch)
	{
		engine->drawImage(image, destinationRect.x, destinationRect.y);
		return true;
	}

	int sourceWidth = 0;
	int sourceHeight = 0;
	if (!engine->getImageSize(image, sourceWidth, sourceHeight) ||
		sourceWidth <= 0 || sourceHeight <= 0)
	{
		return false;
	}

	Rect sourceRect = { 0, 0, sourceWidth, sourceHeight };
	if (cropContent)
	{
		if (cachedCropImage != image)
		{
			cachedCropImage = image;
			cachedCropValid = engine->getImageContentBounds(
				image, cachedCropRect, cropBlack);
		}
		if (cachedCropValid && cachedCropRect.w > 0 &&
			cachedCropRect.h > 0)
		{
			sourceRect = cachedCropRect;
		}
	}

	if (keepAspect)
	{
		engine->drawAspectFitImage(
			image,
			sourceRect,
			destinationRect,
			fadeMirroredBars,
			255,
			fadeMirroredBars ? getTime() : 0);
	}
	else
	{
		engine->drawImage(image, &sourceRect, &destinationRect);
	}
	return true;
}
