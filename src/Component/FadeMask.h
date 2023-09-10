#pragma once
#include "BaseComponent.h"

class FadeMask :
	public BaseComponent
{
public:
	FadeMask();
	virtual ~FadeMask();

	void sleep(UTime t);
	void fadeIn();
	void fadeOut();

private:

	UTime fadeTime = 1000;
	bool isFadeIn = false;
	UTime sleepTime = 0;
	bool isSleep = false;
	UTime fadeBeginTime = 0;
	_shared_image mask = nullptr;

	unsigned char alpha = 0;

protected:
	virtual void onUpdate();
	virtual void onDraw();
	virtual void onRun();
	void freeResource();


};

