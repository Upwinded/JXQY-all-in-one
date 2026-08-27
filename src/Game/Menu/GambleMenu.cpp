#include "GambleMenu.h"
#include "../../Engine/Engine.h"
#include "GambleMenuPolicy.h"
#include "../GameManager/GameManager.h"
#include "../../Component/TextLayout.h"
#include "../../File/File.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
	const char* GambleUiPath = "ini\\ui\\littlegame\\";
	const char* DiceUiManifest = "ini\\ui\\dicegame\\window.ini";
	const char* FishUiManifest = "ini\\ui\\fishgame\\window.ini";
	constexpr double Pi = 3.14159265358979323846;
	const std::array<const char*, 6> DiceTalks = {
		"不要走，决战到天亮。",
		"谁掌控全局谁就能笑到最后。",
		"放人一马会给自己创造机会。",
		"别得意忘形，你很强，别人同样也很强，一山更比一山高。",
		"你们这是自寻死路!",
		"弱，便是你犯下的罪！",
	};

	enum FishMovieIndex
	{
		FishMovieGetFish,
		FishMovieSb01,
		FishMoviePb,
		FishMovieQt,
		FishMovieSb02,
		FishMovieSy,
		FishMovieSh,
		FishMovieYq,
		FishMovieFish,
	};

	std::string gambleUiFile(const std::string& fileName)
	{
		return std::string(GambleUiPath) + fileName;
	}
}

GambleMenu::GambleMenu()
{
	name = "GambleMenu";
	visible = false;
	canCallBack = true;
	rectFullScreen = true;
	setPriority(epMax);
	controllerFocusManager.setInputAwarePresentation();
	init();
}

GambleMenu::~GambleMenu()
{
	freeResource();
}

void GambleMenu::init()
{
	freeResource();

	resourceLayoutLoaded = loadResourceLayout();
	if (resourceLayoutLoaded)
	{
		updateLabels();
		return;
	}

	rect = { 0, 0, 400, 320 };
	align = alCenter;
	setAlign();

	makeLabel(titleLabel, { 24, 24, 352, 30 }, 24, 0xFFFFD37F);
	makeLabel(costLabel, { 24, 62, 352, 28 }, 18, 0xFFFFFFFF);
	makeLabel(diceLabel, { 24, 100, 352, 32 }, 22, 0xFFFFFFFF);
	makeLabel(resultLabel, { 24, 140, 352, 34 }, 18, 0xFFFFFFFF);
	makeButton(decreaseBetButton, { 28, 196, 76, 34 }, "减注");
	makeButton(increaseBetButton, { 116, 196, 76, 34 }, "加注");
	makeButton(smallButton, { 208, 196, 76, 34 }, "押小");
	makeButton(bigButton, { 296, 196, 76, 34 }, "押大");
	makeButton(rollButton, { 76, 254, 100, 36 }, "开盅");
	makeButton(exitButton, { 224, 254, 100, 36 }, "离开");

	setChildRectReferToParent();
	updateLabels();
}

bool GambleMenu::loadResourceLayout()
{
	if (!File::fileExist(gambleUiFile("window.ini")))
	{
		return false;
	}

	initFromIniFileName(gambleUiFile("window.ini"));

	auto addImage = [this](const std::string& fileName) -> std::shared_ptr<ImageContainer>
	{
		std::string path = gambleUiFile(fileName);
		if (!File::fileExist(path))
		{
			return nullptr;
		}
		auto component = std::make_shared<ImageContainer>();
		component->initFromIniFileName(path);
		addChild(component);
		return component;
	};

	auto addButton = [this](const std::string& fileName) -> std::shared_ptr<Button>
	{
		std::string path = gambleUiFile(fileName);
		if (!File::fileExist(path))
		{
			return nullptr;
		}
		auto component = std::make_shared<Button>();
		component->initFromIniFileName(path);
		component->canCallBack = true;
		addChild(component);
		return component;
	};

	auto addTransparentButton = [this](const std::string& fileName) -> std::shared_ptr<Button>
	{
		std::string path = gambleUiFile(fileName);
		if (!File::fileExist(path))
		{
			return nullptr;
		}
		INIReader ini(path);
		auto component = std::make_shared<Button>();
		component->rect.x = ini.GetInteger("Init", "Left", component->rect.x);
		component->rect.y = ini.GetInteger("Init", "Top", component->rect.y);
		component->rect.w = ini.GetInteger("Init", "Width", component->rect.w);
		component->rect.h = ini.GetInteger("Init", "Height", component->rect.h);
		component->hoverSoundEnabled = false;
		component->canCallBack = true;
		component->loadSound(ini.Get("Init", "Sound", ""), 1);
		addChild(component);
		return component;
	};

	auto addLabel = [this](const std::string& fileName) -> std::shared_ptr<Label>
	{
		std::string path = gambleUiFile(fileName);
		if (!File::fileExist(path))
		{
			return nullptr;
		}
		auto component = std::make_shared<Label>();
		component->initFromIniFileName(path);
		addChild(component);
		return component;
	};

	resourceOpenBackground = addImage("openbg.ini");
	resourceGamblingImage = addImage("gambling.ini");
	resourceOpeningImage = addImage("opening.ini");
	resourceDiceImage[0] = addImage("dice1.ini");
	resourceDiceImage[1] = addImage("dice2.ini");
	resourceDiceImage[2] = addImage("dice3.ini");
	resourceGoldImage = addImage("gold.ini");
	resourceMessageBox = addImage("message.ini");
	resourcePlayerFace = addImage("playerface.ini");
	resourceBossFace = addImage("bossface.ini");
	resourceLuFace = addImage("luface.ini");

	resourceChipInButton = addButton("chipin.ini");
	resourceLeaveButton = addButton("quit.ini");
	resourceUpButton = addButton("arrowup.ini");
	resourceDownButton = addButton("arrowdn.ini");
	resourceBigButton = addTransparentButton("gamblebig.ini");
	resourceSmallButton = addTransparentButton("gamblesmall.ini");

	resourcePlayerStakeLabel = addLabel("labplayer.ini");
	resourceDealerStakeLabel = addLabel("labcomputer.ini");
	resourceCurrentBetLabel = addLabel("labchipin.ini");
	resourceMessageLabel = addLabel("labmessage.ini");
	auto centerGambleValueLabel = [](const std::shared_ptr<Label>& label)
	{
		if (label != nullptr)
		{
			label->horizontalAlignment = TextHorizontalAlignment::Center;
		}
	};
	centerGambleValueLabel(resourcePlayerStakeLabel);
	centerGambleValueLabel(resourceDealerStakeLabel);
	centerGambleValueLabel(resourceCurrentBetLabel);
	if (resourceMessageBox != nullptr && resourceMessageLabel != nullptr)
	{
		const auto resolvedRect = GambleMenuPolicy::composeNestedRectangle(
			{
				resourceMessageBox->rect.x,
				resourceMessageBox->rect.y,
				resourceMessageBox->rect.w,
				resourceMessageBox->rect.h,
			},
			{
				resourceMessageLabel->rect.x,
				resourceMessageLabel->rect.y,
				resourceMessageLabel->rect.w,
				resourceMessageLabel->rect.h,
			});
		resourceMessageLabel->rect = {
			resolvedRect.x,
			resolvedRect.y,
			resolvedRect.width,
			resolvedRect.height,
		};
	}

	bool loaded = resourceChipInButton != nullptr
		&& resourceLeaveButton != nullptr
		&& resourceUpButton != nullptr
		&& resourceDownButton != nullptr
		&& resourceBigButton != nullptr
		&& resourceSmallButton != nullptr;
	if (!loaded)
	{
		freeResource();
		return false;
	}

	setChildRectReferToParent();
	return true;
}

bool GambleMenu::loadFishResourceLayout()
{
	std::unique_ptr<char[]> content;
	int length = File::readFile(FishUiManifest, content);
	if (content == nullptr || length <= 0)
	{
		return false;
	}
	INIReader ini(content);
	if (ini.ParseError() != 0
		|| ini.GetInteger("Window", "Width", 0) != 796
		|| ini.GetInteger("Window", "Height", 0) != 569)
	{
		return false;
	}

	auto loadImage = [&ini, this](const std::string& section, const std::string& key)
	{
		std::string path = ini.Get(section, key, "");
		return path.empty() ? _shared_image(nullptr) : engine->loadImageFromFile(path);
	};
	fishFrameImage = loadImage("Window", "Frame");
	fishBackgroundImage = loadImage("Window", "Background");
	fishForegroundImage = loadImage("Window", "Foreground");
	fishProgressBackImage = loadImage("Window", "ProgressBack");
	fishProgressFillImage = loadImage("Window", "ProgressFill");
	fishCastImage = loadImage("Window", "Cast");
	fishPullImage = loadImage("Window", "Pull");
	fishStruggleImage = loadImage("Window", "Struggle");
	fishLifeOnImage = loadImage("Window", "LifeOn");
	fishLifeDownImage = loadImage("Window", "LifeDown");

	const std::array<const char*, 9> movieSections = {
		"MovieGetFish", "MovieSb01", "MoviePb", "MovieQt", "MovieSb02",
		"MovieSy", "MovieSh", "MovieYq", "MovieFish"
	};
	bool loaded = fishFrameImage != nullptr
		&& fishBackgroundImage != nullptr
		&& fishForegroundImage != nullptr
		&& fishProgressBackImage != nullptr
		&& fishProgressFillImage != nullptr
		&& fishCastImage != nullptr
		&& fishPullImage != nullptr
		&& fishStruggleImage != nullptr
		&& fishLifeOnImage != nullptr
		&& fishLifeDownImage != nullptr;
	for (std::size_t index = 0; index < movieSections.size(); index++)
	{
		const std::string section = movieSections[index];
		auto& movie = fishMovies[index];
		movie.image = loadImage(section, "Image");
		movie.cellWidth = ini.GetInteger(section, "CellWidth", 0);
		movie.cellHeight = ini.GetInteger(section, "CellHeight", 0);
		movie.columns = ini.GetInteger(section, "Columns", 0);
		movie.frameCount = ini.GetInteger(section, "Frames", 0);
		movie.interval = ini.GetInteger(section, "Interval", 0);
		movie.drawRect = {
			static_cast<int>(ini.GetInteger(section, "Left", 0)),
			static_cast<int>(ini.GetInteger(section, "Top", 0)),
			static_cast<int>(ini.GetInteger(section, "Width", 0)),
			static_cast<int>(ini.GetInteger(section, "Height", 0)),
		};
		loaded = loaded
			&& movie.image != nullptr
			&& movie.cellWidth > 0 && movie.cellHeight > 0
			&& movie.columns > 0 && movie.frameCount > 0
			&& movie.interval > 0
			&& movie.drawRect.w > 0 && movie.drawRect.h > 0;
	}

	const std::array<const char*, 6> soundKeys = {
		"Cast", "Wait", "Bite", "Pull", "Reel", "Struggle"
	};
	for (std::size_t index = 0; index < soundKeys.size(); index++)
	{
		std::string path = ini.Get("Sounds", soundKeys[index], "");
		fishSounds[index] = path.empty() ? _music(nullptr) : engine->loadSound(path);
	}

	if (!loaded)
	{
		for (auto& sound : fishSounds)
		{
			if (sound != nullptr)
			{
				engine->freeMusic(sound);
				sound = nullptr;
			}
		}
		return false;
	}

	auto addFishButton = [this]()
	{
		auto button = std::make_shared<Button>();
		button->canCallBack = true;
		button->hoverSoundEnabled = false;
		addChild(button);
		return button;
	};
	fishCastButton = addFishButton();
	fishPullButton = addFishButton();
	fishStruggleButton = addFishButton();
	fishReelButton = addFishButton();
	// The reel image contains the pull image. When both are visible after a
	// struggle, the reel action must win the overlapping hit test.
	fishReelButton->setPriority(epItem);
	fishCloseButton = addFishButton();
	fishResourceLoaded = true;
	return true;
}

bool GambleMenu::loadDiceResourceLayout()
{
	std::unique_ptr<char[]> content;
	int length = File::readFile(DiceUiManifest, content);
	if (content == nullptr || length <= 0)
	{
		return false;
	}
	INIReader ini(content);
	if (ini.ParseError() != 0
		|| ini.GetInteger("Window", "Width", 0) != 549
		|| ini.GetInteger("Window", "Height", 0) != 300)
	{
		return false;
	}

	auto loadImage = [&ini, this](const std::string& section, const std::string& key)
	{
		std::string path = ini.Get(section, key, "");
		return path.empty() ? _shared_image(nullptr) : engine->loadImageFromFile(path);
	};
	diceFrameImage = loadImage("Window", "Frame");
	dicePlayerPortraitImage = loadImage("Window", "PlayerPortrait");
	diceNpcPortraitImage = loadImage("Window", "NpcPortrait");
	dicePlayerTalkImage = loadImage("Window", "PlayerTalk");
	diceNpcTalkImage = loadImage("Window", "NpcTalk");
	diceNameplateImage = loadImage("Window", "Nameplate");
	diceSilverImage = loadImage("Window", "Silver");
	diceVersusImage = loadImage("Window", "Versus");
	diceResultImages[0] = loadImage("Window", "ResultWin");
	diceResultImages[1] = loadImage("Window", "ResultLose");
	diceResultImages[2] = loadImage("Window", "ResultTie");
	bool loaded = diceFrameImage != nullptr
		&& dicePlayerPortraitImage != nullptr
		&& diceNpcPortraitImage != nullptr
		&& dicePlayerTalkImage != nullptr
		&& diceNpcTalkImage != nullptr
		&& diceNameplateImage != nullptr
		&& diceSilverImage != nullptr
		&& diceVersusImage != nullptr;
	for (const auto& image : diceResultImages)
	{
		loaded = loaded && image != nullptr;
	}
	for (int face = 1; face <= DiceGambleState::FaceCount; face++)
	{
		diceFaceImages[face - 1] = loadImage("Dice", "Face" + std::to_string(face));
		loaded = loaded && diceFaceImages[face - 1] != nullptr;
	}
	if (!loaded)
	{
		return false;
	}

	auto addDiceButton = [this]()
	{
		auto button = std::make_shared<Button>();
		button->canCallBack = true;
		button->hoverSoundEnabled = false;
		addChild(button);
		return button;
	};
	diceStartButton = addDiceButton();
	diceAddMoneyButton = addDiceButton();
	diceOpenButton = addDiceButton();
	diceCloseButton = addDiceButton();
	diceResourceLoaded = true;
	return true;
}

bool GambleMenu::usesResourceLayout() const
{
	return resourceLayoutLoaded;
}

void GambleMenu::setFallbackControlsVisible(bool controlVisible)
{
	if (titleLabel != nullptr)
	{
		titleLabel->visible = controlVisible;
	}
	if (costLabel != nullptr)
	{
		costLabel->visible = controlVisible;
	}
	if (diceLabel != nullptr)
	{
		diceLabel->visible = controlVisible;
	}
	if (resultLabel != nullptr)
	{
		resultLabel->visible = controlVisible;
	}
	if (decreaseBetButton != nullptr)
	{
		decreaseBetButton->visible = controlVisible && mode == mmGamble;
	}
	if (increaseBetButton != nullptr)
	{
		increaseBetButton->visible = controlVisible
			&& (mode == mmGamble || (mode == mmDiceGame && !diceResourceLoaded));
	}
	if (smallButton != nullptr)
	{
		smallButton->visible = controlVisible && mode == mmGamble;
	}
	if (bigButton != nullptr)
	{
		bigButton->visible = controlVisible && mode == mmGamble;
	}
	if (rollButton != nullptr)
	{
		rollButton->visible = controlVisible;
	}
	if (exitButton != nullptr)
	{
		exitButton->visible = controlVisible;
	}
}

void GambleMenu::setResourceControlsVisible(bool controlVisible)
{
	if (resourceChipInButton != nullptr)
	{
		resourceChipInButton->visible = controlVisible;
	}
	if (resourceLeaveButton != nullptr)
	{
		resourceLeaveButton->visible = controlVisible;
	}
	if (resourceUpButton != nullptr)
	{
		resourceUpButton->visible = controlVisible && mode == mmGamble && !settled;
	}
	if (resourceDownButton != nullptr)
	{
		resourceDownButton->visible = controlVisible && mode == mmGamble && !settled;
	}
	if (resourceBigButton != nullptr)
	{
		resourceBigButton->visible = controlVisible && mode == mmGamble && !settled;
	}
	if (resourceSmallButton != nullptr)
	{
		resourceSmallButton->visible = controlVisible && mode == mmGamble && !settled;
	}
	if (resourceOpenBackground != nullptr)
	{
		resourceOpenBackground->visible = controlVisible;
	}
	if (resourceGamblingImage != nullptr)
	{
		resourceGamblingImage->visible = controlVisible;
	}
	if (resourceOpeningImage != nullptr)
	{
		resourceOpeningImage->visible = controlVisible;
	}
	if (resourceGoldImage != nullptr)
	{
		resourceGoldImage->visible = controlVisible && mode == mmGamble;
	}
	if (resourceMessageBox != nullptr)
	{
		resourceMessageBox->visible = controlVisible;
	}
	if (resourcePlayerFace != nullptr)
	{
		resourcePlayerFace->visible = controlVisible;
	}
	if (resourceBossFace != nullptr)
	{
		resourceBossFace->visible = controlVisible;
	}
	if (resourceLuFace != nullptr)
	{
		resourceLuFace->visible = controlVisible;
	}
	if (resourcePlayerStakeLabel != nullptr)
	{
		resourcePlayerStakeLabel->visible = controlVisible && mode == mmGamble;
	}
	if (resourceDealerStakeLabel != nullptr)
	{
		resourceDealerStakeLabel->visible = controlVisible && mode == mmGamble;
	}
	if (resourceCurrentBetLabel != nullptr)
	{
		resourceCurrentBetLabel->visible = controlVisible && mode == mmGamble;
	}
	if (resourceMessageLabel != nullptr)
	{
		resourceMessageLabel->visible = controlVisible;
	}
	for (auto& diceImage : resourceDiceImage)
	{
		if (diceImage != nullptr)
		{
			diceImage->visible = controlVisible;
		}
	}
}

void GambleMenu::updateResourceLayout()
{
	if (diceResourceLoaded && mode != mmDiceGame)
	{
		diceStartButton->visible = false;
		diceAddMoneyButton->visible = false;
		diceOpenButton->visible = false;
		diceCloseButton->visible = false;
	}
	if (fishResourceLoaded && mode != mmFishGame)
	{
		fishCastButton->visible = false;
		fishPullButton->visible = false;
		fishStruggleButton->visible = false;
		fishReelButton->visible = false;
		fishCloseButton->visible = false;
	}
	if (mode == mmFishGame && fishResourceLoaded)
	{
		setFallbackControlsVisible(false);
		setResourceControlsVisible(false);
		updateFishLayout();
		return;
	}
	if (mode == mmDiceGame && diceResourceLoaded)
	{
		setFallbackControlsVisible(false);
		setResourceControlsVisible(false);
		updateDiceLayout();
		return;
	}
	if (!resourceLayoutLoaded)
	{
		setFallbackControlsVisible(true);
		return;
	}

	setFallbackControlsVisible(false);
	setResourceControlsVisible(true);

	bool showDice = mode == mmGamble ? roundResolved || settled : settled;
	if (resourceGamblingImage != nullptr)
	{
		resourceGamblingImage->visible = mode != mmFishGame && !showDice;
	}
	if (resourceOpeningImage != nullptr)
	{
		resourceOpeningImage->visible = mode != mmFishGame && showDice;
	}
	if (resourceOpenBackground != nullptr)
	{
		resourceOpenBackground->visible = mode != mmFishGame && showDice;
	}

	for (int i = 0; i < 3; i++)
	{
		if (resourceDiceImage[i] != nullptr)
		{
			resourceDiceImage[i]->visible = mode != mmFishGame && showDice;
			resourceDiceImage[i]->frameIndex = std::max(0, dice[i] - 1);
		}
	}

	if (resourcePlayerFace != nullptr)
	{
		resourcePlayerFace->visible = mode == mmGamble;
	}
	if (resourceBossFace != nullptr)
	{
		resourceBossFace->visible = mode == mmGamble && npcType != 0;
	}
	if (resourceLuFace != nullptr)
	{
		resourceLuFace->visible = mode == mmGamble && npcType == 0;
	}
	if (resourceGoldImage != nullptr)
	{
		resourceGoldImage->visible = mode == mmGamble;
	}
	if (resourcePlayerStakeLabel != nullptr)
	{
		resourcePlayerStakeLabel->setStr(std::to_string(playerStake));
	}
	if (resourceDealerStakeLabel != nullptr)
	{
		resourceDealerStakeLabel->setStr(std::to_string(dealerStake));
	}
	if (resourceCurrentBetLabel != nullptr)
	{
		resourceCurrentBetLabel->setStr(std::to_string(currentBet));
	}

	std::string message;
	if (mode == mmFishGame)
	{
		message = settled ? (win ? "收竿完成" : "这次没有收获") : "点击开始";
	}
	else if (mode == mmDiceGame)
	{
		message = settled
			? convert::formatString("骰子:%d %d %d，%s", dice[0], dice[1], dice[2], win ? "赢" : "输")
			: "点击开始";
	}
	else if (settled)
	{
		message = moneyDelta >= 0
			? convert::formatString("你赢了%d两银子", moneyDelta)
			: convert::formatString("你输了%d两银子", -moneyDelta);
	}
	else if (roundResolved)
	{
		int total = dice[0] + dice[1] + dice[2];
		message = convert::formatString("%d点%s，%s",
			total,
			total > 9 ? "大" : "小",
			roundWin ? "你赢了" : "你输了");
	}
	else
	{
		message = currentBet > 0 ? "点击下注开盘" : "选择大小并调整下注";
	}
	if (resourceMessageLabel != nullptr)
	{
		resourceMessageLabel->setStr(message);
	}
}

Rect GambleMenu::scaleDiceRect(const Rect& sourceRect) const
{
	return {
		diceWindowRect.x + static_cast<int>(std::round(sourceRect.x * diceScale)),
		diceWindowRect.y + static_cast<int>(std::round(sourceRect.y * diceScale)),
		std::max(1, static_cast<int>(std::round(sourceRect.w * diceScale))),
		std::max(1, static_cast<int>(std::round(sourceRect.h * diceScale))),
	};
}

void GambleMenu::updateDiceLayout()
{
	if (!diceResourceLoaded)
	{
		return;
	}
	int windowWidth = 0;
	int windowHeight = 0;
	engine->getWindowSize(windowWidth, windowHeight);
	constexpr int margin = 8;
	float widthScale = static_cast<float>(std::max(1, windowWidth - margin * 2)) / 549.0f;
	float heightScale = static_cast<float>(std::max(1, windowHeight - margin * 2)) / 300.0f;
	diceScale = std::min(1.0f, std::min(widthScale, heightScale));
	diceWindowRect.w = std::max(1, static_cast<int>(std::round(549.0f * diceScale)));
	diceWindowRect.h = std::max(1, static_cast<int>(std::round(300.0f * diceScale)));
	diceWindowRect.x = (windowWidth - diceWindowRect.w) / 2;
	diceWindowRect.y = (windowHeight - diceWindowRect.h) / 2;

	diceStartButton->rect = scaleDiceRect({ 443, 263, 82, 28 });
	diceAddMoneyButton->rect = scaleDiceRect({ 351, 263, 82, 28 });
	diceOpenButton->rect = scaleDiceRect({ 260, 262, 82, 29 });
	diceCloseButton->rect = scaleDiceRect({ 521, 4, 24, 24 });
	DiceGambleState::Phase phase = diceState.phase();
	diceStartButton->visible = phase == DiceGambleState::Phase::WaitingForBet;
	diceAddMoneyButton->visible = phase == DiceGambleState::Phase::WaitingForBet;
	diceOpenButton->visible = phase == DiceGambleState::Phase::Rolling;
	diceCloseButton->visible = true;
}

void GambleMenu::resolveDiceRound()
{
	moneyDelta = diceState.lastMoneyDelta();
	if (gm != nullptr && gm->player != nullptr && moneyDelta != 0)
	{
		gm->player->money += moneyDelta;
	}
	if (gm != nullptr && gm->menu != nullptr && gm->menu->goodsMenu != nullptr)
	{
		gm->menu->goodsMenu->updateMoney();
	}
	win = diceState.outcome().result == DiceGambleState::Result::PlayerWin;
	dicePlayerTalk = diceState.outcome().playerTalk;
	diceNpcTalk = diceState.outcome().npcTalk;
	updateLabels();
}

void GambleMenu::updateDiceState()
{
	if (mode != mmDiceGame)
	{
		return;
	}
	UTime now = getTime();
	if (diceLastUpdateTime == 0)
	{
		diceLastUpdateTime = now;
		return;
	}
	UTime elapsed = now >= diceLastUpdateTime ? now - diceLastUpdateTime : 0;
	diceLastUpdateTime = now;
	int elapsedMilliseconds = static_cast<int>(std::min<UTime>(
		elapsed, static_cast<UTime>(std::numeric_limits<int>::max())));
	if (diceState.update(elapsedMilliseconds))
	{
		resolveDiceRound();
	}
	updateDiceLayout();
}

bool GambleMenu::handleDiceControlClick(PElement child)
{
	if (mode != mmDiceGame || !diceResourceLoaded || child == nullptr)
	{
		return false;
	}
	if (child.get() == diceAddMoneyButton.get())
	{
		increaseDiceBet();
	}
	else if (child.get() == diceStartButton.get())
	{
		roll();
	}
	else if (child.get() == diceOpenButton.get())
	{
		roll();
	}
	else if (child.get() == diceCloseButton.get())
	{
		requestExit();
	}
	else
	{
		return false;
	}
	updateLabels();
	return true;
}

void GambleMenu::increaseDiceBet()
{
	if (!diceState.addBet())
	{
		if (gm != nullptr)
		{
			gm->showMessage("银两不足！");
		}
	}
	else if (engine != nullptr)
	{
		dicePlayerTalk = DiceTalks[engine->getRand(5, 0)];
		diceNpcTalk = DiceTalks[engine->getRand(5, 0)];
	}
	updateLabels();
}

Rect GambleMenu::scaleFishRect(const Rect& sourceRect) const
{
	return {
		fishWindowRect.x + static_cast<int>(std::round(sourceRect.x * fishScale)),
		fishWindowRect.y + static_cast<int>(std::round(sourceRect.y * fishScale)),
		std::max(1, static_cast<int>(std::round(sourceRect.w * fishScale))),
		std::max(1, static_cast<int>(std::round(sourceRect.h * fishScale))),
	};
}

void GambleMenu::updateFishLayout()
{
	if (!fishResourceLoaded)
	{
		return;
	}
	int windowWidth = 0;
	int windowHeight = 0;
	engine->getWindowSize(windowWidth, windowHeight);
	constexpr int margin = 8;
	float widthScale = static_cast<float>(std::max(1, windowWidth - margin * 2)) / 796.0f;
	float heightScale = static_cast<float>(std::max(1, windowHeight - margin * 2)) / 569.0f;
	fishScale = std::min(1.0f, std::min(widthScale, heightScale));
	fishWindowRect.w = std::max(1, static_cast<int>(std::round(796.0f * fishScale)));
	fishWindowRect.h = std::max(1, static_cast<int>(std::round(569.0f * fishScale)));
	fishWindowRect.x = (windowWidth - fishWindowRect.w) / 2;
	fishWindowRect.y = (windowHeight - fishWindowRect.h) / 2;

	Rect actionRect = scaleFishRect({ 670, 330, 55, 51 });
	fishCastButton->rect = actionRect;
	fishPullButton->rect = actionRect;
	fishStruggleButton->rect = actionRect;
	fishReelButton->rect = scaleFishRect({ 652, 311, 92, 89 });
	fishCloseButton->rect = scaleFishRect({ 770, 0, 26, 28 });
	fishCastButton->visible = fishState.showCastButton();
	fishPullButton->visible = fishState.showPullButton();
	fishStruggleButton->visible = fishState.showStruggleButton();
	fishReelButton->visible = fishState.showReelButton();
	fishCloseButton->visible = true;
}

void GambleMenu::playFishSound(int index)
{
	if (index >= 0 && index < static_cast<int>(fishSounds.size())
		&& fishSounds[index] != nullptr)
	{
		engine->playSound(fishSounds[index]);
	}
}

void GambleMenu::handleFishEvents(unsigned int events)
{
	if ((events & FishGameState::EventWaitStarted) != 0)
	{
		fishRippleBeginTime = getTime();
		playFishSound(1);
	}
	if ((events & FishGameState::EventBiteStarted) != 0)
	{
		playFishSound(2);
	}
	if ((events & FishGameState::EventCaught) != 0)
	{
		win = true;
		fishTransientTip = "钓上一条大鱼";
		fishTransientTipUntil = getTime() + 1500;
	}
	if ((events & FishGameState::EventEscaped) != 0)
	{
		fishTransientTip = "鱼跑了";
		fishTransientTipUntil = getTime() + 1500;
	}
	updateFishLayout();
}

void GambleMenu::updateFishState()
{
	if (mode != mmFishGame || !fishResourceLoaded)
	{
		return;
	}
	UTime now = getTime();
	if (fishLastUpdateTime == 0)
	{
		fishLastUpdateTime = now;
		return;
	}
	UTime elapsed = now >= fishLastUpdateTime ? now - fishLastUpdateTime : 0;
	fishLastUpdateTime = now;
	int elapsedMilliseconds = static_cast<int>(std::min<UTime>(
		elapsed, static_cast<UTime>(std::numeric_limits<int>::max())));
	auto randomInclusive = [this](int minimum, int maximum)
	{
		return engine->getRand(maximum, minimum);
	};
	handleFishEvents(fishState.update(elapsedMilliseconds, randomInclusive));
}

bool GambleMenu::handleFishControlClick(PElement child)
{
	if (mode != mmFishGame || !fishResourceLoaded || child == nullptr)
	{
		return false;
	}
	if (child.get() == fishCastButton.get())
	{
		roll();
	}
	else if (child.get() == fishPullButton.get())
	{
		pullFishLine();
	}
	else if (child.get() == fishStruggleButton.get())
	{
		makeFishMistake();
	}
	else if (child.get() == fishReelButton.get())
	{
		reelFishLine();
	}
	else if (child.get() == fishCloseButton.get())
	{
		requestExit();
		return true;
	}
	else
	{
		return false;
	}
	return true;
}

void GambleMenu::pullFishLine()
{
	if (engine == nullptr || fishState.phase() != FishGameState::Phase::Pulling)
	{
		return;
	}
	playFishSound(3);
	auto randomInclusive = [this](int minimum, int maximum)
	{
		return engine->getRand(maximum, minimum);
	};
	handleFishEvents(fishState.pull(randomInclusive));
}

void GambleMenu::makeFishMistake()
{
	if (fishState.phase() != FishGameState::Phase::Struggling)
	{
		return;
	}
	playFishSound(5);
	handleFishEvents(fishState.makeMistake());
}

void GambleMenu::reelFishLine()
{
	if (!fishState.canReel())
	{
		return;
	}
	playFishSound(4);
	handleFishEvents(fishState.reel());
}

bool GambleMenu::open(int roundCost, int roundNpcType)
{
	prepareControllerModal();
	resetForRound(roundCost, roundNpcType);
	visible = true;
	configureControllerFocus();
	eventOccupied = true;
	run();
	eventOccupied = false;
	controllerFocusManager.clear();
	visible = false;
	return win;
}

bool GambleMenu::openDiceGame(const std::string& npcName)
{
	prepareControllerModal();
	resetForDiceGame(npcName);
	visible = true;
	configureControllerFocus();
	eventOccupied = true;
	run();
	eventOccupied = false;
	controllerFocusManager.clear();
	visible = false;
	return win;
}

bool GambleMenu::openFishGame()
{
	prepareControllerModal();
	resetForFishGame();
	visible = true;
	configureControllerFocus();
	eventOccupied = true;
	run();
	eventOccupied = false;
	controllerFocusManager.clear();
	visible = false;
	return win;
}

void GambleMenu::resetForRound(int roundCost, int roundNpcType)
{
	mode = mmGamble;
	cost = roundCost;
	npcType = roundNpcType;
	displayName.clear();
	playerStake = roundCost;
	dealerStake = roundCost;
	currentBet = 0;
	moneyDelta = 0;
	dice[0] = 1;
	dice[1] = 1;
	dice[2] = 1;
	settled = false;
	win = false;
	roundResolved = false;
	roundResolvedBeginTime = 0;
	roundWin = false;
	betBig = false;
	updateLabels();
}

void GambleMenu::resetForDiceGame(const std::string& npcName)
{
	if (!diceResourceLoaded)
	{
		loadDiceResourceLayout();
	}
	mode = mmDiceGame;
	cost = 0;
	npcType = 0;
	displayName = npcName;
	playerStake = 0;
	dealerStake = 0;
	currentBet = 0;
	moneyDelta = 0;
	dice[0] = 1;
	dice[1] = 1;
	dice[2] = 1;
	settled = false;
	win = false;
	roundResolved = false;
	roundResolvedBeginTime = 0;
	roundWin = false;
	betBig = false;
	int playerMoney = gm != nullptr && gm->player != nullptr ? gm->player->money : 0;
	diceState.reset(playerMoney, DiceGambleState::BetStep);
	diceLastUpdateTime = getTime();
	dicePlayerTalk = DiceTalks[engine->getRand(5, 0)];
	diceNpcTalk = DiceTalks[engine->getRand(5, 0)];
	updateLabels();
}

void GambleMenu::resetForFishGame()
{
	if (!fishResourceLoaded)
	{
		loadFishResourceLayout();
	}
	mode = mmFishGame;
	cost = 0;
	npcType = 0;
	displayName.clear();
	playerStake = 0;
	dealerStake = 0;
	currentBet = 0;
	moneyDelta = 0;
	dice[0] = 1;
	dice[1] = 1;
	dice[2] = 1;
	settled = false;
	win = false;
	roundResolved = false;
	roundResolvedBeginTime = 0;
	roundWin = false;
	betBig = false;
	fishState.reset();
	fishLastUpdateTime = getTime();
	fishOpenTime = fishLastUpdateTime;
	fishRippleBeginTime = 0;
	fishTransientTipUntil = 0;
	fishTransientTip.clear();
	updateLabels();
}

void GambleMenu::roll()
{
	if (settled)
	{
		return;
	}

	if (mode == mmFishGame)
	{
		if (fishState.showCastButton())
		{
			playFishSound(0);
			handleFishEvents(fishState.startCast());
		}
		return;
	}
	if (mode == mmDiceGame)
	{
		if (diceState.phase() == DiceGambleState::Phase::WaitingForBet)
		{
			if (!diceState.start())
			{
				if (gm != nullptr)
				{
					gm->showMessage("请先下注！");
				}
			}
		}
		else if (diceState.phase() == DiceGambleState::Phase::Rolling)
		{
			if (engine == nullptr)
			{
				return;
			}
			std::array<int, DiceGambleState::DiceCount> playerDice;
			std::array<int, DiceGambleState::DiceCount> npcDice;
			for (int index = 0; index < DiceGambleState::DiceCount; index++)
			{
				playerDice[index] = engine->getRand(DiceGambleState::FaceCount, 1);
				npcDice[index] = engine->getRand(DiceGambleState::FaceCount, 1);
			}
			if (diceState.open(playerDice, npcDice) && diceState.update(0))
			{
				resolveDiceRound();
			}
		}
		updateLabels();
		return;
	}

	if (currentBet <= 0)
	{
		if (gm != nullptr)
		{
			gm->showMessage("不下注，开什么开！");
		}
		return;
	}
	if (engine == nullptr)
	{
		return;
	}

	int total = 0;
	for (int i = 0; i < 3; i++)
	{
		dice[i] = engine->getRand(6, 1);
		total += dice[i];
	}

	if (mode == mmGamble)
	{
		bool diceBig = total > 9;
		roundWin = diceBig == betBig;
		if (roundWin)
		{
			playerStake += currentBet;
			dealerStake -= currentBet;
		}
		else
		{
			playerStake -= currentBet;
			dealerStake += currentBet;
		}
		roundResolved = true;
		roundResolvedBeginTime = getTime();
		if (playerStake <= 0 || dealerStake <= 0)
		{
			finishGamble();
		}
		updateLabels();
		return;
	}

}

void GambleMenu::increaseBet()
{
	if (mode != mmGamble || settled || playerStake <= 0)
	{
		return;
	}
	playerStake--;
	currentBet++;
	roundResolved = false;
	roundResolvedBeginTime = 0;
	updateLabels();
}

void GambleMenu::decreaseBet()
{
	if (mode != mmGamble || settled || currentBet <= 0)
	{
		return;
	}
	currentBet--;
	playerStake++;
	roundResolved = false;
	roundResolvedBeginTime = 0;
	updateLabels();
}

void GambleMenu::updateGambleRoundState()
{
	constexpr UTime ResultDisplayMilliseconds = 1500;
	if (mode != mmGamble || settled || !roundResolved)
	{
		return;
	}

	UTime now = getTime();
	if (now < roundResolvedBeginTime
		|| now - roundResolvedBeginTime < ResultDisplayMilliseconds)
	{
		return;
	}

	roundResolved = false;
	roundResolvedBeginTime = 0;
	updateLabels();
}

void GambleMenu::finishGamble()
{
	if (mode != mmGamble || settled)
	{
		return;
	}
	moneyDelta = playerStake + currentBet - cost;
	win = moneyDelta >= 0;
	settled = true;
	if (gm->player != nullptr)
	{
		gm->player->money = std::max(0, gm->player->money + moneyDelta);
	}
	if (gm->menu != nullptr && gm->menu->goodsMenu != nullptr)
	{
		gm->menu->goodsMenu->updateMoney();
	}
	if (moneyDelta >= 0)
	{
		gm->showMessage(convert::formatString("你赢了%d两银子", moneyDelta));
	}
	else
	{
		gm->showMessage(convert::formatString("你输了%d两银子", -moneyDelta));
	}
	updateLabels();
}

void GambleMenu::updateLabels()
{
	if (titleLabel != nullptr)
	{
		if (mode == mmDiceGame)
		{
			titleLabel->setStr(displayName.empty() ? "骰子小游戏" : "骰子：" + displayName);
		}
		else if (mode == mmFishGame)
		{
			titleLabel->setStr("钓鱼");
		}
		else
		{
			titleLabel->setStr(npcType == 0 ? "赌局：吕文才" : "赌局：赌场老板");
		}
	}
	if (costLabel != nullptr)
	{
		if (mode == mmDiceGame)
		{
			costLabel->setStr(convert::formatString(
				"银两:%d  下注:%d",
				diceState.playerMoney(),
				diceState.stake()));
		}
		else if (mode == mmFishGame)
		{
			costLabel->setStr("静待浮漂，见机收竿");
		}
		else
		{
			costLabel->setStr(convert::formatString(
				"本金:%d  玩家:%d  庄家:%d  注:%d",
				cost,
				playerStake,
				dealerStake,
				currentBet));
		}
	}
	if (diceLabel != nullptr)
	{
		if (mode == mmFishGame)
		{
			diceLabel->setStr(fishState.tip().empty() ? "鱼漂尚未入水" : fishState.tip());
		}
		else
		{
			bool showDice = mode == mmGamble ? roundResolved : true;
			std::string diceText;
			if (mode == mmDiceGame)
			{
				diceText = convert::formatString(
					"你:%d %d %d  对手:%d %d %d",
					diceState.displayedFace(true, 0),
					diceState.displayedFace(true, 1),
					diceState.displayedFace(true, 2),
					diceState.displayedFace(false, 0),
					diceState.displayedFace(false, 1),
					diceState.displayedFace(false, 2));
			}
			else
			{
				diceText = showDice
					? convert::formatString("骰子：%d  %d  %d", dice[0], dice[1], dice[2])
					: "骰子：?  ?  ?";
			}
			if (mode == mmGamble)
			{
				diceText += betBig ? "  押大" : "  押小";
			}
			diceLabel->setStr(diceText);
		}
	}
	if (resultLabel != nullptr)
	{
		std::string resultText;
		if (mode == mmFishGame)
		{
			resultText = fishState.showCastButton() ? "点击抛竿开始" : fishState.tip();
		}
		else if (mode == mmDiceGame)
		{
			resultText = diceState.outcome().result == DiceGambleState::Result::None
				? "下注后点击开始，再点击开盅"
				: dicePlayerTalk;
		}
		else
		{
			if (settled)
			{
				if (moneyDelta == 0)
				{
					resultText = "赌局结束：没有输赢";
				}
				else
				{
					resultText = win ? "赌局结束：你赢了！" : "赌局结束：你输了！";
				}
			}
			else if (roundResolved)
			{
				resultText = roundWin ? "本轮押中，可继续下注或离开" : "本轮未中，可继续下注或离开";
			}
			else
			{
				resultText = "选择大小并调整下注";
			}
		}
		resultLabel->setStr(resultText);
	}
	if (rollButton != nullptr)
	{
		if (settled && mode != mmDiceGame)
		{
			rollButton->setStr("确定");
		}
		else if (mode == mmFishGame)
		{
			rollButton->setStr("抛竿");
		}
		else if (mode == mmDiceGame)
		{
			rollButton->setStr(diceState.phase() == DiceGambleState::Phase::Rolling ? "开盅" : "开始");
		}
		else
		{
			rollButton->setStr("开盅");
		}
	}
	if (decreaseBetButton != nullptr)
	{
		decreaseBetButton->visible = mode == mmGamble;
	}
	if (increaseBetButton != nullptr)
	{
		increaseBetButton->visible = mode == mmGamble
			|| (mode == mmDiceGame && !diceResourceLoaded);
		if (mode == mmDiceGame)
		{
			increaseBetButton->setStr("下注50");
		}
	}
	if (smallButton != nullptr)
	{
		smallButton->visible = mode == mmGamble;
		smallButton->setStr(betBig ? "押小" : "押小*");
	}
	if (bigButton != nullptr)
	{
		bigButton->visible = mode == mmGamble;
		bigButton->setStr(betBig ? "押大*" : "押大");
	}
	if (exitButton != nullptr)
	{
		exitButton->setStr(settled ? "关闭" : "离开");
	}
	updateResourceLayout();
}

void GambleMenu::settleOnExit()
{
	if (settled)
	{
		return;
	}
	if (mode == mmGamble)
	{
		finishGamble();
		return;
	}
	if (mode == mmFishGame)
	{
		win = fishState.catchCount() > 0;
	}
	else if (mode == mmDiceGame)
	{
		win = diceState.outcome().result == DiceGambleState::Result::PlayerWin;
	}
	else
	{
		win = false;
	}
	settled = true;

	updateLabels();
}

void GambleMenu::prepareControllerModal()
{
	controllerFocusManager.clear();
	if (gm == nullptr || gm->menu == nullptr)
	{
		return;
	}
	gm->menu->cancelControllerInteraction();
	if (gm->menu->toolTip != nullptr)
	{
		gm->menu->toolTip->hide();
	}
}

void GambleMenu::configureControllerFocus()
{
	controllerFocusManager.clear();
	if (mode != mmGamble)
	{
		return;
	}

	const PElement decreaseControl = usesResourceLayout()
		? std::static_pointer_cast<Element>(resourceDownButton)
		: std::static_pointer_cast<Element>(decreaseBetButton);
	const PElement increaseControl = usesResourceLayout()
		? std::static_pointer_cast<Element>(resourceUpButton)
		: std::static_pointer_cast<Element>(increaseBetButton);
	const PElement smallControl = usesResourceLayout()
		? std::static_pointer_cast<Element>(resourceSmallButton)
		: std::static_pointer_cast<Element>(smallButton);
	const PElement bigControl = usesResourceLayout()
		? std::static_pointer_cast<Element>(resourceBigButton)
		: std::static_pointer_cast<Element>(bigButton);
	const PElement primaryControl = usesResourceLayout()
		? std::static_pointer_cast<Element>(resourceChipInButton)
		: std::static_pointer_cast<Element>(rollButton);
	const PElement exitControl = usesResourceLayout()
		? std::static_pointer_cast<Element>(resourceLeaveButton)
		: std::static_pointer_cast<Element>(exitButton);

	auto makeBinding = [this](
		const std::string& id,
		const PElement& control)
	{
		return UIFocusNodeBinding{
			id,
			control,
			[this, control]() { handleControlClick(control); },
		};
	};
	controllerFocusManager.addVisualSpatialGroup(
		"gamble-controls",
		{
			makeBinding("gamble-decrease", decreaseControl),
			makeBinding("gamble-increase", increaseControl),
			makeBinding("gamble-small", smallControl),
			makeBinding("gamble-big", bigControl),
			makeBinding("gamble-primary", primaryControl),
			makeBinding("gamble-exit", exitControl),
		});
	controllerFocusManager.setDefaultFocus("gamble-increase");
	controllerFocusManager.focusDefault();
}

void GambleMenu::requestExit()
{
	settleOnExit();
	logicRunning = false;
}

bool GambleMenu::handleDiceUIAction(UIAction action)
{
	if (action == UIAction::Confirm)
	{
		const DiceGambleState::Phase phase = diceState.phase();
		if (phase == DiceGambleState::Phase::WaitingForBet
			|| phase == DiceGambleState::Phase::Rolling)
		{
			roll();
		}
		return true;
	}
	if (action == UIAction::PageNext)
	{
		if (diceState.phase() == DiceGambleState::Phase::WaitingForBet)
		{
			increaseDiceBet();
		}
		return true;
	}
	return false;
}

bool GambleMenu::handleFishUIAction(UIAction action)
{
	if (action == UIAction::Confirm)
	{
		switch (fishState.phase())
		{
		case FishGameState::Phase::Idle:
			roll();
			break;
		case FishGameState::Phase::Pulling:
			pullFishLine();
			break;
		default:
			break;
		}
		return true;
	}
	if (action == UIAction::PageNext)
	{
		if (fishState.canReel())
		{
			reelFishLine();
		}
		return true;
	}
	return false;
}

std::vector<ControllerPromptItem> GambleMenu::controllerPromptItems() const
{
	using GameInput::InputAction;
	std::vector<ControllerPromptItem> items;
	switch (mode)
	{
	case mmGamble:
		if (!settled)
		{
			items.push_back({ InputAction::NavigateUp, "选择" });
			items.push_back({ InputAction::Confirm, "执行" });
		}
		items.push_back({ InputAction::Cancel, "离开" });
		break;
	case mmDiceGame:
		switch (diceState.phase())
		{
		case DiceGambleState::Phase::WaitingForBet:
			items.push_back({ InputAction::Confirm, "开始" });
			items.push_back({ InputAction::NextPage, "下注50" });
			break;
		case DiceGambleState::Phase::Rolling:
			items.push_back({ InputAction::Confirm, "开盅" });
			break;
		case DiceGambleState::Phase::Revealing:
		default:
			break;
		}
		items.push_back({ InputAction::Cancel, "离开" });
		break;
	case mmFishGame:
		switch (fishState.phase())
		{
		case FishGameState::Phase::Idle:
			items.push_back({ InputAction::Confirm, "抛竿" });
			break;
		case FishGameState::Phase::Pulling:
			items.push_back({ InputAction::Confirm, "拉线" });
			if (fishState.canReel())
			{
				items.push_back({ InputAction::NextPage, "提竿" });
			}
			break;
		case FishGameState::Phase::Struggling:
			if (fishState.canReel())
			{
				items.push_back({ InputAction::NextPage, "提竿" });
			}
			break;
		case FishGameState::Phase::Casting:
		case FishGameState::Phase::Waiting:
		case FishGameState::Phase::Biting:
		default:
			break;
		}
		items.push_back({ InputAction::Cancel, "离开" });
		break;
	default:
		break;
	}
	return items;
}

void GambleMenu::drawControllerPrompts(const Rect& windowBounds)
{
	if (engine == nullptr)
	{
		return;
	}

	int windowWidth = 0;
	int windowHeight = 0;
	engine->getWindowSize(windowWidth, windowHeight);
	Rect bounds = windowBounds;
	if (bounds.w <= 0 || bounds.h <= 0)
	{
		bounds = { 0, 0, windowWidth, windowHeight };
	}

	const float layoutScale = mode == mmDiceGame
		? diceScale : (mode == mmFishGame ? fishScale : 1.0f);
	const int desiredHeight = std::clamp(
		static_cast<int>(std::round(36.0f * layoutScale)), 30, 44);
	ControllerPromptDrawOptions options;
	options.x = std::max(0, bounds.x + 4);
	options.width = std::max(1,
		std::min(bounds.w - 8, windowWidth - options.x));
	options.height = desiredHeight;
	const int spaceBelow = windowHeight - (bounds.y + bounds.h) - 4;
	if (spaceBelow >= desiredHeight)
	{
		options.y = bounds.y + bounds.h + 4;
	}
	else
	{
		options.y = std::max(bounds.y,
			bounds.y + bounds.h - desiredHeight - 4);
	}
	options.fontSize = std::clamp(
		static_cast<int>(std::round(14.0f * layoutScale)), 11, 17);
	options.itemGap = std::clamp(
		static_cast<int>(std::round(14.0f * layoutScale)), 9, 18);
	ControllerPromptPresenter::draw(
		engine, engine->inputActions(), controllerPromptItems(), options);
}

void GambleMenu::makeLabel(std::shared_ptr<Label>& label, const Rect& labelRect, int fontSize, unsigned int color)
{
	label = std::make_shared<Label>();
	label->rect = labelRect;
	label->fontSize = fontSize;
	label->color = color;
	label->coverMouse = false;
	addChild(label);
}

void GambleMenu::makeButton(std::shared_ptr<TextButton>& button, const Rect& buttonRect, const std::string& text)
{
	button = std::make_shared<TextButton>();
	button->rect = buttonRect;
	button->setFontSize(18);
	button->setStrColor(0xFFFFFFFF);
	button->setStr(text);
	button->loadSound("sound\\界-大按钮.wav", 1);
	addChild(button);
}

bool GambleMenu::handleControlClick(PElement child)
{
	if (handleFishControlClick(child))
	{
		return true;
	}
	if (handleDiceControlClick(child))
	{
		return true;
	}
	if (mode == mmDiceGame && child != nullptr
		&& child.get() == increaseBetButton.get())
	{
		increaseDiceBet();
		return true;
	}
	using namespace GambleMenuPolicy;
	Control control = Control::None;
	if (child.get() == decreaseBetButton.get() || child.get() == resourceDownButton.get())
	{
		control = Control::DecreaseBet;
	}
	else if (child.get() == increaseBetButton.get() || child.get() == resourceUpButton.get())
	{
		control = Control::IncreaseBet;
	}
	else if (child.get() == smallButton.get() || child.get() == resourceSmallButton.get())
	{
		control = Control::Small;
	}
	else if (child.get() == bigButton.get() || child.get() == resourceBigButton.get())
	{
		control = Control::Big;
	}
	else if (child.get() == rollButton.get() || child.get() == resourceChipInButton.get())
	{
		control = Control::Primary;
	}
	else if (child.get() == exitButton.get() || child.get() == resourceLeaveButton.get())
	{
		control = Control::Exit;
	}

	switch (actionForClick(control, mode == mmGamble, settled))
	{
	case Action::DecreaseBet:
		decreaseBet();
		break;
	case Action::IncreaseBet:
		increaseBet();
		break;
	case Action::SelectSmall:
		betBig = false;
		updateLabels();
		break;
	case Action::SelectBig:
		betBig = true;
		updateLabels();
		break;
	case Action::Roll:
		roll();
		break;
	case Action::Close:
	case Action::SettleAndClose:
		requestExit();
		break;
	default:
		return false;
	}
	return true;
}

void GambleMenu::onChildCallBack(PElement child)
{
	if (!visible || child == nullptr || !child->getResult(erClick))
	{
		return;
	}
	handleControlClick(child);
}

void GambleMenu::onEvent()
{
	if (!visible)
	{
		return;
	}
	updateGambleRoundState();
	updateFishState();
	updateDiceState();

	auto pollClick = [this](const PElement& child)
	{
		if (child == nullptr || !child->getResult(erClick))
		{
			return false;
		}
		handleControlClick(child);
		return true;
	};

	if (mode == mmFishGame && fishResourceLoaded)
	{
		if (pollClick(fishCastButton)
			|| pollClick(fishPullButton)
			|| pollClick(fishStruggleButton)
			|| pollClick(fishReelButton)
			|| pollClick(fishCloseButton))
		{
			return;
		}
		return;
	}
	if (mode == mmDiceGame && diceResourceLoaded)
	{
		if (pollClick(diceAddMoneyButton)
			|| pollClick(diceStartButton)
			|| pollClick(diceOpenButton)
			|| pollClick(diceCloseButton))
		{
			return;
		}
		return;
	}

	if (usesResourceLayout())
	{
		if (pollClick(resourceDownButton)
			|| pollClick(resourceUpButton)
			|| pollClick(resourceSmallButton)
			|| pollClick(resourceBigButton)
			|| pollClick(resourceChipInButton)
			|| pollClick(resourceLeaveButton))
		{
			return;
		}
	}

	if (pollClick(decreaseBetButton)
		|| pollClick(increaseBetButton)
		|| pollClick(smallButton)
		|| pollClick(bigButton)
		|| pollClick(rollButton)
		|| pollClick(exitButton))
	{
		return;
	}
}

bool GambleMenu::onHandleEvent(AEvent & e)
{
	if (!visible)
	{
		return false;
	}
	if ((e.eventType == ET_MOUSEDOWN
			&& e.eventData == MBC_MOUSE_LEFT)
		|| e.eventType == ET_FINGERDOWN)
	{
		adoptUIFocusPointerTarget(
			e.eventType == ET_MOUSEDOWN ? TOUCH_MOUSEID : e.eventData);
	}

	if (e.eventType == ET_QUIT)
	{
		settleOnExit();
		stop(erExit);
		if (gm != nullptr)
		{
			gm->result |= erExit;
			gm->setRunning(false);
		}
		return true;
	}

	return dispatchKeyboardUIAction(e, *this);
}

bool GambleMenu::onHandleUIAction(UIAction action)
{
	if (!visible)
	{
		return false;
	}
	if (action == UIAction::Cancel)
	{
		requestExit();
		return true;
	}
	switch (mode)
	{
	case mmGamble:
		return controllerFocusManager.handleAction(action);
	case mmDiceGame:
		return handleDiceUIAction(action);
	case mmFishGame:
		return handleFishUIAction(action);
	default:
		return false;
	}
}

void GambleMenu::drawDiceImage(const _shared_image& image, const Rect& sourceLayoutRect)
{
	if (image == nullptr)
	{
		return;
	}
	int sourceWidth = 0;
	int sourceHeight = 0;
	if (!engine->getImageSize(image, sourceWidth, sourceHeight))
	{
		return;
	}
	Rect source = { 0, 0, sourceWidth, sourceHeight };
	Rect destination = scaleDiceRect(sourceLayoutRect);
	engine->drawImage(image, &source, &destination);
}

void GambleMenu::drawDiceGame()
{
	updateDiceLayout();
	drawDiceImage(diceFrameImage, { 0, 0, 549, 300 });
	drawDiceImage(dicePlayerTalkImage, { 84, 51, 180, 61 });
	drawDiceImage(diceNpcTalkImage, { 295, 51, 180, 61 });
	drawDiceImage(diceNameplateImage, { 15, 167, 167, 37 });
	drawDiceImage(diceNameplateImage, { 369, 170, 167, 37 });
	drawDiceImage(dicePlayerPortraitImage, { 16, 48, 70, 70 });
	drawDiceImage(diceNpcPortraitImage, { 460, 46, 70, 70 });
	drawDiceImage(diceSilverImage, { 15, 273, 18, 11 });

	const std::array<int, DiceGambleState::DiceCount> playerX = { 35, 81, 127 };
	const std::array<int, DiceGambleState::DiceCount> npcX = { 388, 434, 480 };
	for (int index = 0; index < DiceGambleState::DiceCount; index++)
	{
		int playerFace = diceState.displayedFace(true, index);
		int npcFace = diceState.displayedFace(false, index);
		drawDiceImage(diceFaceImages[playerFace - 1], { playerX[index], 167, 37, 37 });
		drawDiceImage(diceFaceImages[npcFace - 1], { npcX[index], 169, 37, 37 });
	}

	if (diceState.phase() == DiceGambleState::Phase::WaitingForBet
		&& diceState.outcome().result != DiceGambleState::Result::None)
	{
		int resultIndex = 2;
		if (diceState.outcome().result == DiceGambleState::Result::PlayerWin)
		{
			resultIndex = 0;
		}
		else if (diceState.outcome().result == DiceGambleState::Result::NpcWin)
		{
			resultIndex = 1;
		}
		drawDiceImage(diceResultImages[resultIndex], { 222, 133, 106, 106 });
	}
	else
	{
		drawDiceImage(diceVersusImage, { 218, 151, 120, 74 });
	}

	int fontSize = std::max(10, static_cast<int>(std::round(12 * diceScale)));
	int titleSize = std::max(13, static_cast<int>(std::round(18 * diceScale)));
	Rect titleRect = scaleDiceRect({ 0, 2, 549, 29 });
	engine->drawText("骰子", titleRect.x + titleRect.w / 2 - titleSize,
		titleRect.y + std::max(1, static_cast<int>(3 * diceScale)), titleSize, 0xFFFFFFFF);
	engine->drawText("×", scaleDiceRect({ 525, 5, 18, 20 }).x,
		scaleDiceRect({ 525, 5, 18, 20 }).y, titleSize, 0xFFFFFFFF);
	std::string playerName = gm != nullptr && gm->player != nullptr && !gm->player->npcName.empty()
		? gm->player->npcName : "玩家";
	std::string npcName = displayName.empty() ? "庄家" : displayName;
	Rect playerNameRect = scaleDiceRect({ 17, 100, 75, 19 });
	Rect npcNameRect = scaleDiceRect({ 471, 100, 60, 19 });
	engine->drawText(playerName, playerNameRect.x, playerNameRect.y, fontSize, 0xFF333333);
	engine->drawText(npcName, npcNameRect.x, npcNameRect.y, fontSize, 0xFF333333);

	auto drawWrappedTalk = [this, fontSize](const std::string& talk, const Rect& layoutRect)
	{
		Rect talkRect = scaleDiceRect(layoutRect);
		int charactersPerLine = TextLayout::charactersPerLineForWidth(talkRect.w, fontSize);
		auto lines = TextLayout::wrapUtf8Text(talk, charactersPerLine);
		int lineGap = std::max(1, static_cast<int>(3 * diceScale));
		int lineHeight = fontSize + lineGap;
		int maximumLines = std::max(1, (talkRect.h + lineGap) / lineHeight);
		for (int index = 0; index < std::min(maximumLines, static_cast<int>(lines.size())); index++)
		{
			engine->drawText(lines[index], talkRect.x, talkRect.y + index * lineHeight,
				fontSize, 0xFF666666);
		}
	};
	drawWrappedTalk(dicePlayerTalk, { 100, 60, 153, 42 });
	drawWrappedTalk(diceNpcTalk, { 308, 60, 153, 42 });
	Rect betRect = scaleDiceRect({ 36, 267, 85, 19 });
	engine->drawText(std::to_string(diceState.stake()), betRect.x, betRect.y,
		fontSize, 0xFFFFFFFF);

	auto drawButton = [this, fontSize](const std::shared_ptr<Button>& button, const std::string& text)
	{
		if (button == nullptr || !button->visible)
		{
			return;
		}
		engine->fillRect(button->rect.x, button->rect.y, button->rect.w, button->rect.h,
			68, 49, 30, 235);
		engine->fillRect(button->rect.x + 1, button->rect.y + 1, button->rect.w - 2, 1,
			224, 184, 98, 255);
		int textWidth = static_cast<int>(TextLayout::countUtf8Characters(text)) * fontSize;
		engine->drawText(text, button->rect.x + std::max(2, (button->rect.w - textWidth) / 2),
			button->rect.y + std::max(2, (button->rect.h - fontSize) / 2), fontSize, 0xFFFFFFFF);
	};
	drawButton(diceAddMoneyButton, "下注50");
	drawButton(diceStartButton, "开始");
	drawButton(diceOpenButton, "开盅");
}

void GambleMenu::drawFishImage(const _shared_image& image, const Rect& sourceLayoutRect)
{
	if (image == nullptr)
	{
		return;
	}
	int sourceWidth = 0;
	int sourceHeight = 0;
	if (!engine->getImageSize(image, sourceWidth, sourceHeight))
	{
		return;
	}
	Rect source = { 0, 0, sourceWidth, sourceHeight };
	Rect destination = scaleFishRect(sourceLayoutRect);
	engine->drawImage(image, &source, &destination);
}

void GambleMenu::drawFishMovie(const FishMovie& movie, int frameIndex)
{
	if (movie.image == nullptr || movie.frameCount <= 0 || movie.columns <= 0)
	{
		return;
	}
	frameIndex = std::clamp(frameIndex, 0, movie.frameCount - 1);
	Rect source = {
		(frameIndex % movie.columns) * movie.cellWidth,
		(frameIndex / movie.columns) * movie.cellHeight,
		movie.cellWidth,
		movie.cellHeight,
	};
	Rect destination = scaleFishRect(movie.drawRect);
	engine->drawImage(movie.image, &source, &destination);
}

void GambleMenu::drawFishProgress()
{
	Rect progressLayout = { 288, 182, 227, 218 };
	drawFishImage(fishProgressBackImage, progressLayout);
	if (fishProgressFillImage == nullptr || fishState.tension() <= 0)
	{
		return;
	}

	const float width = static_cast<float>(progressLayout.w);
	const float height = static_cast<float>(progressLayout.h);
	const float centerX = width * 0.5f;
	const float centerY = height * 0.5f;
	const double startAngle = Pi * 0.5;
	const double sweep = 2.0 * Pi * std::clamp(fishState.tension() / 100.0, 0.0, 1.0);
	if (sweep <= 0.0)
	{
		return;
	}

	auto normalize = [](double angle)
	{
		while (angle < 0.0)
		{
			angle += 2.0 * Pi;
		}
		while (angle >= 2.0 * Pi)
		{
			angle -= 2.0 * Pi;
		}
		return angle;
	};
	auto rayToEdge = [centerX, centerY, width, height](double angle)
	{
		double directionX = std::cos(angle);
		double directionY = std::sin(angle);
		double distance = std::numeric_limits<double>::max();
		if (directionX > 0.000001)
		{
			distance = std::min(distance, (width - centerX) / directionX);
		}
		else if (directionX < -0.000001)
		{
			distance = std::min(distance, -centerX / directionX);
		}
		if (directionY > 0.000001)
		{
			distance = std::min(distance, (height - centerY) / directionY);
		}
		else if (directionY < -0.000001)
		{
			distance = std::min(distance, -centerY / directionY);
		}
		return SDL_FPoint {
			static_cast<float>(centerX + directionX * distance),
			static_cast<float>(centerY + directionY * distance),
		};
	};

	struct BoundaryPoint
	{
		double delta = 0.0;
		SDL_FPoint point = { 0.0f, 0.0f };
	};
	std::vector<BoundaryPoint> boundary;
	boundary.push_back({ 0.0, rayToEdge(startAngle) });
	const std::array<SDL_FPoint, 4> corners = {
		SDL_FPoint { 0.0f, height },
		SDL_FPoint { 0.0f, 0.0f },
		SDL_FPoint { width, 0.0f },
		SDL_FPoint { width, height },
	};
	for (const auto& corner : corners)
	{
		double angle = std::atan2(corner.y - centerY, corner.x - centerX);
		double delta = normalize(angle - startAngle);
		if (delta > 0.000001 && delta < sweep - 0.000001)
		{
			boundary.push_back({ delta, corner });
		}
	}
	std::sort(boundary.begin(), boundary.end(), [](const BoundaryPoint& left, const BoundaryPoint& right)
	{
		return left.delta < right.delta;
	});
	boundary.push_back({ sweep, rayToEdge(startAngle + sweep) });

	Rect destination = scaleFishRect(progressLayout);
	auto makeVertex = [&destination, width, height](float x, float y)
	{
		Vertex vertex;
		vertex.position = {
			destination.x + x / width * destination.w,
			destination.y + y / height * destination.h,
		};
		vertex.color = { 1.0f, 1.0f, 1.0f, 1.0f };
		vertex.tex_coord = { x / width, y / height };
		return vertex;
	};
	std::vector<Vertex> vertices;
	vertices.push_back(makeVertex(centerX, centerY));
	for (const auto& point : boundary)
	{
		vertices.push_back(makeVertex(point.point.x, point.point.y));
	}
	std::vector<int> indices;
	for (int index = 1; index + 1 < static_cast<int>(vertices.size()); index++)
	{
		indices.push_back(0);
		indices.push_back(index);
		indices.push_back(index + 1);
	}
	engine->drawGeometry(fishProgressFillImage, vertices, indices);
}

void GambleMenu::drawFishGame()
{
	updateFishLayout();
	UTime now = getTime();
	auto ambientFrame = [now, this](const FishMovie& movie)
	{
		UTime elapsed = now >= fishOpenTime ? now - fishOpenTime : 0;
		return static_cast<int>((elapsed / movie.interval) % movie.frameCount);
	};

	drawFishImage(fishFrameImage, { 0, 0, 796, 569 });
	drawFishImage(fishBackgroundImage, { 5, 29, 786, 534 });
	drawFishImage(fishForegroundImage, { 5, 29, 786, 534 });
	drawFishMovie(fishMovies[FishMovieSb01], ambientFrame(fishMovies[FishMovieSb01]));
	drawFishMovie(fishMovies[FishMoviePb], ambientFrame(fishMovies[FishMoviePb]));
	drawFishMovie(fishMovies[FishMovieQt], ambientFrame(fishMovies[FishMovieQt]));
	drawFishMovie(fishMovies[FishMovieSb02], ambientFrame(fishMovies[FishMovieSb02]));
	if (fishState.waterRippleStarted())
	{
		UTime rippleElapsed = now >= fishRippleBeginTime ? now - fishRippleBeginTime : 0;
		drawFishMovie(fishMovies[FishMovieSy],
			static_cast<int>((rippleElapsed / fishMovies[FishMovieSy].interval)
				% fishMovies[FishMovieSy].frameCount));
	}
	drawFishMovie(fishMovies[FishMovieSh], ambientFrame(fishMovies[FishMovieSh]));
	drawFishMovie(fishMovies[FishMovieYq], ambientFrame(fishMovies[FishMovieYq]));

	int fishFrame = 1;
	int elapsed = fishState.phaseElapsedMilliseconds();
	switch (fishState.phase())
	{
	case FishGameState::Phase::Idle:
		fishFrame = 1 + static_cast<int>(((now - fishOpenTime) / 125) % 7);
		break;
	case FishGameState::Phase::Casting:
		fishFrame = 8 + std::min(10, elapsed / 125);
		break;
	case FishGameState::Phase::Waiting:
		fishFrame = 19 + (elapsed / 125) % 8;
		break;
	case FishGameState::Phase::Biting:
		fishFrame = 26 + std::min(1, elapsed / 125);
		break;
	case FishGameState::Phase::Pulling:
	case FishGameState::Phase::Struggling:
		fishFrame = 28 + (elapsed / 125) % 5;
		break;
	}
	drawFishMovie(fishMovies[FishMovieFish], fishFrame);

	if (fishState.showFishBar())
	{
		drawFishProgress();
		for (int index = 0; index < FishGameState::MaximumLives; index++)
		{
			const auto& image = index < fishState.lostLives() ? fishLifeDownImage : fishLifeOnImage;
			drawFishImage(image, { 307 + index * 47, 402, 28, 28 });
		}
	}
	if (fishState.showCastButton())
	{
		drawFishImage(fishCastImage, { 670, 330, 55, 51 });
	}
	if (fishState.showPullButton())
	{
		drawFishImage(fishPullImage, { 670, 330, 55, 51 });
	}
	if (fishState.showStruggleButton())
	{
		drawFishImage(fishStruggleImage, { 670, 330, 55, 51 });
	}
	if (fishState.showReelButton())
	{
		drawFishMovie(fishMovies[FishMovieGetFish], ambientFrame(fishMovies[FishMovieGetFish]));
	}

	int titleFontSize = std::max(12, static_cast<int>(std::round(18 * fishScale)));
	engine->drawText("钓鱼", fishWindowRect.x + fishWindowRect.w / 2 - titleFontSize,
		fishWindowRect.y + std::max(2, static_cast<int>(4 * fishScale)), titleFontSize, 0xFFF1E2C0);
	engine->drawText("×", scaleFishRect({ 777, 3, 16, 20 }).x,
		scaleFishRect({ 777, 3, 16, 20 }).y, titleFontSize, 0xFFFFFFFF);
	std::string tip = now < fishTransientTipUntil ? fishTransientTip : fishState.tip();
	if (!tip.empty())
	{
		Rect tipRect = scaleFishRect({ 8, 414, 253, 43 });
		engine->drawText(tip, tipRect.x, tipRect.y,
			std::max(11, static_cast<int>(std::round(16 * fishScale))), 0xFFFFFFFF);
	}
}

void GambleMenu::onDraw()
{
	int windowWidth = 0;
	int windowHeight = 0;
	engine->getWindowSize(windowWidth, windowHeight);
	engine->fillRect(0, 0, windowWidth, windowHeight, 0, 0, 0, 120);
	if (mode == mmFishGame && fishResourceLoaded)
	{
		drawFishGame();
		return;
	}
	if (mode == mmDiceGame && diceResourceLoaded)
	{
		drawDiceGame();
		return;
	}

	if (usesResourceLayout())
	{
		ImageContainer::onDraw();
		std::shared_ptr<Button> selectedBetButton = betBig ? resourceBigButton : resourceSmallButton;
		if (mode == mmGamble && !settled && selectedBetButton != nullptr && selectedBetButton->visible)
		{
			engine->fillRect(
				selectedBetButton->rect.x,
				selectedBetButton->rect.y,
				selectedBetButton->rect.w,
				selectedBetButton->rect.h,
				255,
				220,
				80,
				50);
		}
		return;
	}

	engine->fillRect(rect.x, rect.y, rect.w, rect.h, 28, 20, 12, 230);
	engine->fillRect(rect.x + 2, rect.y + 2, rect.w - 4, 2, 215, 166, 78, 255);
	engine->fillRect(rect.x + 2, rect.y + rect.h - 4, rect.w - 4, 2, 92, 58, 20, 255);
	if (decreaseBetButton != nullptr && decreaseBetButton->visible)
	{
		engine->fillRect(decreaseBetButton->rect.x, decreaseBetButton->rect.y, decreaseBetButton->rect.w, decreaseBetButton->rect.h, 72, 52, 34, 230);
	}
	if (increaseBetButton != nullptr && increaseBetButton->visible)
	{
		engine->fillRect(increaseBetButton->rect.x, increaseBetButton->rect.y, increaseBetButton->rect.w, increaseBetButton->rect.h, 92, 58, 20, 230);
	}
	if (smallButton != nullptr && smallButton->visible)
	{
		engine->fillRect(smallButton->rect.x, smallButton->rect.y, smallButton->rect.w, smallButton->rect.h, betBig ? 62 : 92, betBig ? 46 : 58, betBig ? 32 : 20, 230);
	}
	if (bigButton != nullptr && bigButton->visible)
	{
		engine->fillRect(bigButton->rect.x, bigButton->rect.y, bigButton->rect.w, bigButton->rect.h, betBig ? 92 : 62, betBig ? 58 : 46, betBig ? 20 : 32, 230);
	}
	if (rollButton != nullptr)
	{
		engine->fillRect(rollButton->rect.x, rollButton->rect.y, rollButton->rect.w, rollButton->rect.h, 92, 58, 20, 230);
	}
	if (exitButton != nullptr)
	{
		engine->fillRect(exitButton->rect.x, exitButton->rect.y, exitButton->rect.w, exitButton->rect.h, 62, 46, 32, 230);
	}
}

void GambleMenu::onDrawEnd()
{
	if (!ControllerPromptPresenter::canPresentForOwner(
		this, ControllerPromptOwnerPolicy::CurrentRunOwner))
	{
		return;
	}
	if (mode == mmFishGame && fishResourceLoaded)
	{
		drawControllerPrompts(fishWindowRect);
		return;
	}
	if (mode == mmDiceGame && diceResourceLoaded)
	{
		drawControllerPrompts(diceWindowRect);
		return;
	}
	drawControllerPrompts(rect);
}

void GambleMenu::freeResource()
{
	controllerFocusManager.clear();
	diceFrameImage = nullptr;
	dicePlayerPortraitImage = nullptr;
	diceNpcPortraitImage = nullptr;
	dicePlayerTalkImage = nullptr;
	diceNpcTalkImage = nullptr;
	diceNameplateImage = nullptr;
	diceSilverImage = nullptr;
	diceVersusImage = nullptr;
	for (auto& image : diceResultImages)
	{
		image = nullptr;
	}
	for (auto& image : diceFaceImages)
	{
		image = nullptr;
	}
	diceStartButton = nullptr;
	diceAddMoneyButton = nullptr;
	diceOpenButton = nullptr;
	diceCloseButton = nullptr;
	diceResourceLoaded = false;
	for (auto& sound : fishSounds)
	{
		if (sound != nullptr)
		{
			engine->freeMusic(sound);
			sound = nullptr;
		}
	}
	for (auto& movie : fishMovies)
	{
		movie = FishMovie();
	}
	fishFrameImage = nullptr;
	fishBackgroundImage = nullptr;
	fishForegroundImage = nullptr;
	fishProgressBackImage = nullptr;
	fishProgressFillImage = nullptr;
	fishCastImage = nullptr;
	fishPullImage = nullptr;
	fishStruggleImage = nullptr;
	fishLifeOnImage = nullptr;
	fishLifeDownImage = nullptr;
	fishCastButton = nullptr;
	fishPullButton = nullptr;
	fishStruggleButton = nullptr;
	fishReelButton = nullptr;
	fishCloseButton = nullptr;
	fishResourceLoaded = false;
	titleLabel = nullptr;
	costLabel = nullptr;
	diceLabel = nullptr;
	resultLabel = nullptr;
	decreaseBetButton = nullptr;
	increaseBetButton = nullptr;
	smallButton = nullptr;
	bigButton = nullptr;
	rollButton = nullptr;
	exitButton = nullptr;
	resourceChipInButton = nullptr;
	resourceLeaveButton = nullptr;
	resourceUpButton = nullptr;
	resourceDownButton = nullptr;
	resourceBigButton = nullptr;
	resourceSmallButton = nullptr;
	resourceGamblingImage = nullptr;
	resourceOpeningImage = nullptr;
	resourceOpenBackground = nullptr;
	for (auto& diceImage : resourceDiceImage)
	{
		diceImage = nullptr;
	}
	resourcePlayerFace = nullptr;
	resourceBossFace = nullptr;
	resourceLuFace = nullptr;
	resourceMessageBox = nullptr;
	resourceGoldImage = nullptr;
	resourcePlayerStakeLabel = nullptr;
	resourceDealerStakeLabel = nullptr;
	resourceCurrentBetLabel = nullptr;
	resourceMessageLabel = nullptr;
	resourceLayoutLoaded = false;
	Panel::freeResource();
	removeAllChild();
}
