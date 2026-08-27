#pragma once
#include "../../Component/Component.h"
#include "../../Component/Button.h"
#include "../../Engine/AspectFitLayout.h"
#include "../../Weather/Weather.h"
#include "../Menu/UIFocusManager.h"

class GamepadEssentialUITestAccess;

class Title :
	public ConfigDrivenPanel
{
	friend class GamepadEssentialUITestAccess;
public:
	Title(bool skipStartupVideos = false);
	virtual ~Title();

	virtual void init() override;

	void playTitleBGM();
	void freeResource();
private:
	std::shared_ptr<Button> initBtn = nullptr;
	std::shared_ptr<Button> exitBtn = nullptr;
	std::shared_ptr<Button> loadBtn = nullptr;
	std::shared_ptr<Button> teamBtn = nullptr;
	std::shared_ptr<Weather> weather = nullptr;
	_shared_image titleCompositionCanvas = nullptr;
	_image titleCompositionOriginalTarget = nullptr;
	int titleCompositionCanvasWidth = 0;
	int titleCompositionCanvasHeight = 0;
	bool drawingTitleComposition = false;
	bool skipStartupVideos = false;
	std::vector<AspectFitPointerRipple> pointerRipples;
	UIFocusManager focusManager;

	bool ensureTitleCompositionCanvas();
	void removeExpiredPointerRipples();
	void configureFocus();
	void startNewGame();
	void openSavedGame();
	void openTeamPage();
	void exitApplication();

	virtual void onEvent() override;
	virtual bool onInitial();
	virtual void onExit();
	virtual void onRun();
	virtual void onPreviewPointerEvent(AEvent& e) override;
	virtual bool onHandleEvent(AEvent & e);
	virtual bool onHandleUIAction(UIAction action) override;
	virtual bool onBeginDrawComposition() override;
	virtual bool shouldDrawChildAfterComposition(
		const PElement& child) const override;
	virtual void onEndDrawComposition(bool completed) override;
	virtual void onDraw() override;
	virtual void onDrawEnd() override;
};
