#pragma once
#include "ImageContainer.h"
class TransImage :
	public ImageContainer
{
public:
	TransImage();
	~TransImage();

	unsigned char alpha = 96;

	virtual void onDraw();
};

