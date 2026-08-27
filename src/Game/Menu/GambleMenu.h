#pragma once
#include <array>
#include <string>
#include <vector>
#include "../../Component/Component.h"
#include "ControllerPromptPresenter.h"
#include "DiceGambleState.h"
#include "FishGameState.h"
#include "UIFocusManager.h"

class GambleMenuTestAccess;

class GambleMenu :
	public Panel
{
	friend class GambleMenuTestAccess;
public:
	GambleMenu();
	virtual ~GambleMenu();

	bool open(int cost, int npcType);
	bool openDiceGame(const std::string& npcName);
	bool openFishGame();

	virtual void init() override;
	virtual void onChildCallBack(PElement child) override;

private:
	int cost = 0;
	int npcType = 0;
	int dice[3] = { 1, 1, 1 };
	int playerStake = 0;
	int dealerStake = 0;
	int currentBet = 0;
	int moneyDelta = 0;
	bool settled = false;
	bool win = false;
	bool roundResolved = false;
	UTime roundResolvedBeginTime = 0;
	bool roundWin = false;
	bool betBig = false;
	std::string displayName;

	enum MenuMode
	{
		mmGamble,
		mmDiceGame,
		mmFishGame
	};
	MenuMode mode = mmGamble;

	std::shared_ptr<Label> titleLabel = nullptr;
	std::shared_ptr<Label> costLabel = nullptr;
	std::shared_ptr<Label> diceLabel = nullptr;
	std::shared_ptr<Label> resultLabel = nullptr;
	std::shared_ptr<TextButton> decreaseBetButton = nullptr;
	std::shared_ptr<TextButton> increaseBetButton = nullptr;
	std::shared_ptr<TextButton> smallButton = nullptr;
	std::shared_ptr<TextButton> bigButton = nullptr;
	std::shared_ptr<TextButton> rollButton = nullptr;
	std::shared_ptr<TextButton> exitButton = nullptr;
	std::shared_ptr<Button> resourceChipInButton = nullptr;
	std::shared_ptr<Button> resourceLeaveButton = nullptr;
	std::shared_ptr<Button> resourceUpButton = nullptr;
	std::shared_ptr<Button> resourceDownButton = nullptr;
	std::shared_ptr<Button> resourceBigButton = nullptr;
	std::shared_ptr<Button> resourceSmallButton = nullptr;
	std::shared_ptr<ImageContainer> resourceGamblingImage = nullptr;
	std::shared_ptr<ImageContainer> resourceOpeningImage = nullptr;
	std::shared_ptr<ImageContainer> resourceOpenBackground = nullptr;
	std::shared_ptr<ImageContainer> resourceDiceImage[3] = { nullptr, nullptr, nullptr };
	std::shared_ptr<ImageContainer> resourcePlayerFace = nullptr;
	std::shared_ptr<ImageContainer> resourceBossFace = nullptr;
	std::shared_ptr<ImageContainer> resourceLuFace = nullptr;
	std::shared_ptr<ImageContainer> resourceMessageBox = nullptr;
	std::shared_ptr<ImageContainer> resourceGoldImage = nullptr;
	std::shared_ptr<Label> resourcePlayerStakeLabel = nullptr;
	std::shared_ptr<Label> resourceDealerStakeLabel = nullptr;
	std::shared_ptr<Label> resourceCurrentBetLabel = nullptr;
	std::shared_ptr<Label> resourceMessageLabel = nullptr;
	bool resourceLayoutLoaded = false;

	DiceGambleState diceState;
	_shared_image diceFrameImage = nullptr;
	_shared_image dicePlayerPortraitImage = nullptr;
	_shared_image diceNpcPortraitImage = nullptr;
	_shared_image dicePlayerTalkImage = nullptr;
	_shared_image diceNpcTalkImage = nullptr;
	_shared_image diceNameplateImage = nullptr;
	_shared_image diceSilverImage = nullptr;
	_shared_image diceVersusImage = nullptr;
	std::array<_shared_image, 3> diceResultImages = { nullptr, nullptr, nullptr };
	std::array<_shared_image, DiceGambleState::FaceCount> diceFaceImages = {
		nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
	};
	std::shared_ptr<Button> diceStartButton = nullptr;
	std::shared_ptr<Button> diceAddMoneyButton = nullptr;
	std::shared_ptr<Button> diceOpenButton = nullptr;
	std::shared_ptr<Button> diceCloseButton = nullptr;
	bool diceResourceLoaded = false;
	float diceScale = 1.0f;
	Rect diceWindowRect = { 0, 0, 0, 0 };
	UTime diceLastUpdateTime = 0;
	std::string dicePlayerTalk;
	std::string diceNpcTalk;

	struct FishMovie
	{
		_shared_image image = nullptr;
		int cellWidth = 0;
		int cellHeight = 0;
		int columns = 0;
		int frameCount = 0;
		int interval = 0;
		Rect drawRect = { 0, 0, 0, 0 };
	};

	FishGameState fishState;
	std::array<FishMovie, 9> fishMovies;
	_shared_image fishFrameImage = nullptr;
	_shared_image fishBackgroundImage = nullptr;
	_shared_image fishForegroundImage = nullptr;
	_shared_image fishProgressBackImage = nullptr;
	_shared_image fishProgressFillImage = nullptr;
	_shared_image fishCastImage = nullptr;
	_shared_image fishPullImage = nullptr;
	_shared_image fishStruggleImage = nullptr;
	_shared_image fishLifeOnImage = nullptr;
	_shared_image fishLifeDownImage = nullptr;
	std::array<_music, 6> fishSounds = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	std::shared_ptr<Button> fishCastButton = nullptr;
	std::shared_ptr<Button> fishPullButton = nullptr;
	std::shared_ptr<Button> fishStruggleButton = nullptr;
	std::shared_ptr<Button> fishReelButton = nullptr;
	std::shared_ptr<Button> fishCloseButton = nullptr;
	bool fishResourceLoaded = false;
	float fishScale = 1.0f;
	Rect fishWindowRect = { 0, 0, 0, 0 };
	UTime fishLastUpdateTime = 0;
	UTime fishOpenTime = 0;
	UTime fishRippleBeginTime = 0;
	UTime fishTransientTipUntil = 0;
	std::string fishTransientTip;
	UIFocusManager controllerFocusManager;

	void resetForRound(int roundCost, int roundNpcType);
	void resetForDiceGame(const std::string& npcName);
	void resetForFishGame();
	bool loadResourceLayout();
	bool loadDiceResourceLayout();
	bool loadFishResourceLayout();
	bool usesResourceLayout() const;
	void setFallbackControlsVisible(bool visible);
	void setResourceControlsVisible(bool visible);
	void updateResourceLayout();
	void updateGambleRoundState();
	void updateDiceLayout();
	void updateDiceState();
	bool handleDiceControlClick(PElement child);
	void increaseDiceBet();
	void resolveDiceRound();
	void drawDiceGame();
	void drawDiceImage(const _shared_image& image, const Rect& sourceLayoutRect);
	Rect scaleDiceRect(const Rect& sourceRect) const;
	void updateFishLayout();
	void updateFishState();
	void handleFishEvents(unsigned int events);
	bool handleFishControlClick(PElement child);
	void pullFishLine();
	void makeFishMistake();
	void reelFishLine();
	void playFishSound(int index);
	void drawFishGame();
	void drawFishMovie(const FishMovie& movie, int frameIndex);
	void drawFishImage(const _shared_image& image, const Rect& sourceLayoutRect);
	void drawFishProgress();
	Rect scaleFishRect(const Rect& sourceRect) const;
	void roll();
	void increaseBet();
	void decreaseBet();
	void finishGamble();
	void updateLabels();
	void settleOnExit();
	void prepareControllerModal();
	void configureControllerFocus();
	void requestExit();
	bool handleDiceUIAction(UIAction action);
	bool handleFishUIAction(UIAction action);
	std::vector<ControllerPromptItem> controllerPromptItems() const;
	void drawControllerPrompts(const Rect& windowBounds);
	void makeLabel(std::shared_ptr<Label>& label, const Rect& labelRect, int fontSize, unsigned int color);
	void makeButton(std::shared_ptr<TextButton>& button, const Rect& buttonRect, const std::string& text);
	bool handleControlClick(PElement child);

	virtual void onEvent() override;
	virtual bool onHandleEvent(AEvent & e) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onDraw() override;
	virtual void onDrawEnd() override;
	virtual void freeResource() override;
};
