#pragma once
#include "../../Component/Component.h"
#include "../Config/Config.h"
#include "UIFocusManager.h"

#include <vector>

class Option :
	public ConfigDrivenPanel
{
public:
	Option();
	virtual ~Option();
	virtual void init() override;

	std::shared_ptr<Button> rtnBtn = nullptr;
	std::shared_ptr<Scrollbar> music = nullptr;
	std::shared_ptr<Scrollbar> sound = nullptr;
	std::shared_ptr<Scrollbar> speed = nullptr;
	std::shared_ptr<CheckBox> playerAlpha = nullptr;
	std::shared_ptr<CheckBox> dyLoad = nullptr;
	std::shared_ptr<CheckBox> shadow = nullptr;
	std::shared_ptr<ImageContainer> playerBg = nullptr;
	std::shared_ptr<ImageContainer> dyLoadBg = nullptr;
	std::shared_ptr<ImageContainer> shadowBg = nullptr;

	std::shared_ptr<CheckBox> musicCB = nullptr;
	std::shared_ptr<CheckBox> soundCB = nullptr;
	std::shared_ptr<CheckBox> speedCB = nullptr;
	UIFocusManager focusManager;

	std::shared_ptr<FlatTextButton> touchControlsButton = nullptr;
	std::shared_ptr<FlatTextButton> cheatSettingsButton = nullptr;

private:
	struct ElementPresentationState
	{
		PElement element;
		bool visible = false;
		bool activated = false;
	};

	void freeResource();
	void configureFocus(const std::string& preferredFocusId);
	void configureCheatPanelFocus(const std::string& preferredFocusId);
	void closeMenu();
	bool adjustScrollbar(const std::shared_ptr<Scrollbar>& scrollbar, int direction);
	void applyMusicPosition();
	void applySoundPosition();
	void applySpeedPosition();
	void applyMusicCheckBox();
	void applySoundCheckBox();
	void toggleMusicMute();
	void toggleSoundMute();
	void resetSpeedToDefault();
	void applyPlayerAlpha();
	void togglePlayerAlpha();
	void applyDynamicLoading();
	void toggleDynamicLoading();

	void createFooterOptions();
	void layoutFooterOptions();
	void syncTouchControlsOption();
	void toggleTouchControlsOption();
	void createCheatPanel();
	void layoutCheatPanel();
	void syncCheatPanel();
	void setCheatPanelResult(const std::string& message);
	void setCheatPanelElementsVisible(bool visible);
	bool isCheatPanelElement(const PElement& element) const;
	void openCheatPanel(
		const std::string& preferredFocusId = "",
		const std::string& returnFocusId = "");
	void closeCheatPanel();
	void toggleCheatModeOption();
	void toggleInvincibilityOption();
	Rect getCheatPanelRect() const;

	std::shared_ptr<Label> cheatPanelTitle = nullptr;
	std::shared_ptr<Label> cheatResultLabel = nullptr;
	std::shared_ptr<FlatTextButton> cheatModeButton = nullptr;
	std::shared_ptr<FlatTextButton> invincibilityButton = nullptr;
	std::shared_ptr<FlatTextButton> restoreResourcesButton = nullptr;
	std::shared_ptr<FlatTextButton> increaseMagicLevelButton = nullptr;
	std::shared_ptr<FlatTextButton> increasePlayerLevelButton = nullptr;
	std::shared_ptr<FlatTextButton> addMoneyButton = nullptr;
	std::shared_ptr<FlatTextButton> cheatPanelBackButton = nullptr;
	std::vector<ElementPresentationState> mainOptionPresentation;
	std::string mainOptionsFocusId;
	bool cheatPanelVisible = false;

	int musicPos = 0;
	int soundPos = 0;
	int speedPos = 0;

	int speedToPos(float spd);
	float posToSpeed(int pos);

	virtual void onEvent() override;

	virtual bool onHandleEvent(AEvent & e) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onDraw() override;
	virtual void onDrawEnd() override;
	virtual void onRun() override;
	virtual void onChildCallBack(PElement child) override;
};
