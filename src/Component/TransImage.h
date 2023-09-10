#pragma once
#include "ImageContainer.h"
class TransImage :
	public ImageContainer
{
public:
	TransImage();
	virtual ~TransImage();

	unsigned char alpha = 72;

	virtual void onDraw();
};

