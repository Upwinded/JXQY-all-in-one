#pragma once
#include "../Element/Element.h"

class FadeMask :
	public Element
{
public:
	FadeMask();
	~FadeMask();

	void sleep(unsigned int t);
	void fadeIn();
	void fadeOut();

private:

	unsigned int fadeTime = 1000;
	bool isFadeIn = false;
	unsigned int sleepTime = 0;
	bool isSleep = false;
	unsigned int fadeBeginTime = 0;
	_image mask = NULL;

	unsigned char alpha = 0;

	virtual void onUpdate();
	virtual void onDraw();
	virtual void onRun();


};

