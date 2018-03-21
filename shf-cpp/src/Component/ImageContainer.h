#pragma once
#include "../Element/Element.h"

class ImageContainer :
	public Element
{
public:
	ImageContainer();
	~ImageContainer();

	IMPImage * impImage = NULL;

	bool stretch = false;

	virtual void freeResource();

	virtual void initFromIni(const std::string & fileName);

private:
	virtual void onDraw();
};

