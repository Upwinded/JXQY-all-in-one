#pragma once
#include "ImageContainer.h"


#define freeCom(component); \
	removeChild(component);\
	if (component != nullptr)\
	{\
		component = nullptr;\
	}


class Panel :
	public ImageContainer
{
public:
	Panel();
	virtual ~Panel();

	virtual void init() {}

	Align align = alNone;
	int alignX = 0;
	int alignY = 0;
	int baseWidth = 0;
	int baseHeight = 0;
	float scale = 1.0f;
	void setAlign();
public:
	virtual void initFromIni(INIReader & ini);

	virtual void getChildScaleFactor(float& scaleX, float& scaleY) override;
	virtual void getChildLayoutOffset(int& offsetX, int& offsetY) override;
protected:

	virtual void freeResource() override;
	virtual void onWindowResize(int width, int height) override;

	void resetRect(PElement e, int x, int y);
	void resetRect();

};
