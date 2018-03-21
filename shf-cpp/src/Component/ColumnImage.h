#pragma once
#include "ImageContainer.h"
class ColumnImage :
	public ImageContainer
{
public:
	ColumnImage();
	~ColumnImage();

	double percent = 1.0;

private:
	virtual void onDraw();
};

