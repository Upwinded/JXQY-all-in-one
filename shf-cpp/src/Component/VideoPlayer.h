#pragma once
#include "..\Element\Element.h"

class VideoPlayer :
	public Element
{
public:
	VideoPlayer();
	~VideoPlayer();
	VideoPlayer(const std::string & fileName);

	_video v = NULL;

	std::string videoFileName = "";
	
	int loop = 0;

	void reopenVideo(const std::string& fileName, int vloop = 0);

	virtual void freeResource();

protected:

	virtual bool onHandleEvent(AEvent * e);

	virtual bool onInitial();
	virtual void onExit();
	virtual void onRun();
	virtual void onUpdate();
	virtual void onDraw();
	virtual void onDrawDrag();
	virtual void onClick();
	virtual void onDragEnd(Element * dst);
	virtual void onDrag(int * param1, int * param2);
};

