#pragma once
#include "BaseComponent.h"

class ImageContainer :
	public BaseComponent
{
public:
	ImageContainer();
	virtual ~ImageContainer();

	_shared_imp impImage = nullptr;

	bool stretch = false;
	bool keepAspect = false;
	bool fadeMirroredBars = false;
	bool cropContent = false;
	bool cropBlack = false;
	int frameIndex = -1;

	virtual void initFromIni(INIReader & ini);

protected:
	void freeResource();
	_shared_image cachedCropImage = nullptr;
	Rect cachedCropRect = { 0, 0, 0, 0 };
	bool cachedCropValid = false;

public:
	virtual void onDraw();
	bool drawImagetoRect(Rect destinationRect, bool drawStretch);
};
