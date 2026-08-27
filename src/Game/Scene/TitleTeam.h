#pragma once
#include "../../Element/Element.h"
#include "VideoPage.h"
#include "../Data/GameElement.h"
#include <string>
#include <vector>

class TitleTeam :
	public Element
{
	friend class MobileExternalInputRuntimeTestAccess;
	friend class GamepadEssentialUITestAccess;
public:
	TitleTeam(const std::string& videoFileName,
		const std::string& teamInfoText = "");
	virtual ~TitleTeam();
	std::shared_ptr<VideoPage> vp = nullptr;

	void freeResource();
	virtual void onChildCallBack(PElement child);
private:
	virtual bool onInitial();
	virtual void onDrawEnd();
	virtual void onExit();
	virtual bool onHandleEvent(AEvent & e);
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onUpdate();
	virtual bool onPointerInteractionCanceled(EventTouchID pointerID) override;
	virtual void onAllPointerInteractionsCanceled() override;
	void updateTextLayout();
	void scrollByLines(int delta);
	void closePage();
	bool beginTextPointer(EventTouchID pointerId, int x, int y);
	bool updateTextPointer(EventTouchID pointerId, int y);
	bool finishTextPointer(EventTouchID pointerId);
	void resetTextPointer();

	std::string videoFileName;
	std::string teamInfoText;
	std::vector<std::string> wrappedTeamInfoLines;
	Rect textRect = { 0, 0, 0, 0 };
	Rect closeRect = { 0, 0, 0, 0 };
	int textFontSize = 24;
	int textLineGap = 6;
	int firstVisibleLine = 0;
	int visibleLineCount = 0;
	int layoutWindowWidth = -1;
	int layoutWindowHeight = -1;
	EventTouchID activePointer = TOUCH_UNTOUCHEDID;
	int pointerStartY = 0;
	int pointerStartLine = 0;
};
