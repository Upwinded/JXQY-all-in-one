#pragma once
#include "../../Component/Component.h"
#include "VideoPlayer.h"
class TitleTeam :
	public Element
{
public:
	TitleTeam();
	~TitleTeam();
	VideoPlayer * vp = NULL;

	virtual void freeResource();

private:
	virtual bool onInitial();
	virtual void onDraw();
	virtual void onExit();
	virtual bool onHandleEvent(AEvent * e);
	virtual void onUpdate();
};

