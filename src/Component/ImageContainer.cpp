#include "ImageContainer.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"
#include <algorithm>

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
	nineSlice = nineSliceWidth = 0;
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
	nineSlice = std::max(0, static_cast<int>(ini.GetInteger("Init", "NineSlice", 0)));
	nineSliceWidth = std::max(0, static_cast<int>(ini.GetInteger("Init", "NineSliceWidth", nineSlice)));
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
	if (nineSlice > 0 && destinationRect.w > 0 && destinationRect.h > 0)
	{
		const int sourceBorder = std::min({ nineSlice, sourceWidth / 2, sourceHeight / 2 });
		const int border = std::min({ nineSliceWidth, destinationRect.w / 2, destinationRect.h / 2 });
		const int sourceX[] = { 0, sourceBorder, sourceWidth - sourceBorder, sourceWidth };
		const int sourceY[] = { 0, sourceBorder, sourceHeight - sourceBorder, sourceHeight };
		const int targetX[] = { 0, border, destinationRect.w - border, destinationRect.w };
		const int targetY[] = { 0, border, destinationRect.h - border, destinationRect.h };
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 3; ++column)
			{
				Rect source{ sourceX[column], sourceY[row], sourceX[column + 1] - sourceX[column], sourceY[row + 1] - sourceY[row] };
				Rect target{ destinationRect.x + targetX[column], destinationRect.y + targetY[row], targetX[column + 1] - targetX[column], targetY[row + 1] - targetY[row] };
				if (source.w > 0 && source.h > 0 && target.w > 0 && target.h > 0)
				{
					engine->drawImage(image, &source, &target);
				}
			}
		}
		return true;
	}
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
