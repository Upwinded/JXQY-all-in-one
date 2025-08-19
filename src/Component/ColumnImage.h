#pragma once
#include "ImageContainer.h"
class ColumnImage :
	public ImageContainer
{
public:
	ColumnImage();
	virtual ~ColumnImage();

	float percent = 1.0;

private:
	virtual void onDraw();
};

