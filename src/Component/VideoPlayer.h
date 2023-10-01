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

#ifdef __MOBILE__
	std::shared_ptr<Label> skipLabel = std::make_shared<Label>();
#endif

public:
	virtual void onChildCallBack(PElement child);

protected:

	virtual bool onHandleEvent(AEvent & e);

	virtual bool onInitial();
	virtual void onExit();
	virtual void onRun();
	virtual void onUpdate();
	virtual void onDraw();
	virtual void onDrawDrag(int x, int y);
	virtual void onClick();
	virtual void onDragEnd(PElement dst, int x, int y);
	virtual void onDragBegin(int * param1, int * param2);
};

