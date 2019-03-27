#pragma once
#include "../../Component/Component.h"
#include "VideoPage.h"
class TitleTeam :
	public Element
{
public:
	TitleTeam();
	~TitleTeam();
	VideoPage * vp = NULL;

	virtual void freeResource();

private:
	virtual bool onInitial();
	virtual void onDraw();
	virtual void onExit();
	virtual bool onHandleEvent(AEvent * e);
	virtual void onUpdate();
};

