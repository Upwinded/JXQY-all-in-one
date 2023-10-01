#pragma once
#include "../../Component/Component.h"
#include "VideoPage.h"
#include "../Data/GameElement.h"
class TitleTeam :
	public Element
{
public:
	TitleTeam();
	virtual ~TitleTeam();
	std::shared_ptr<VideoPage> vp = nullptr;

	void freeResource();
	virtual void onChildCallBack(PElement child);
private:
	virtual bool onInitial();
	virtual void onDraw();
	virtual void onExit();
	virtual bool onHandleEvent(AEvent & e);
	virtual void onUpdate();
};

