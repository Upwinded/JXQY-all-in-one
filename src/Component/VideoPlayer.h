#pragma once
#include "BaseComponent.h"
#include "Label.h"

class VideoPlayer :
	public BaseComponent
{
public:
	VideoPlayer();
	virtual ~VideoPlayer();
	VideoPlayer(const std::string & fileName);

	_video v = nullptr;

	std::string videoFileName = "";
	
	int loop = 0;

	void reopenVideo(const std::string& fileName, int vloop = 0);

	void freeResource();

#ifdef _MOBILE
	Label skipLabel;
#endif

public:
	virtual void onChildCallBack(Element * child);

protected:

	virtual bool onHandleEvent(AEvent * e);

	virtual bool onInitial();
	virtual void onExit();
	virtual void onRun();
	virtual void onUpdate();
	virtual void onDraw();
	virtual void onDrawDrag(int x, int y);
	virtual void onClick();
	virtual void onDragEnd(Element * dst, int x, int y);
	virtual void onDragBegin(int * param1, int * param2);
};

