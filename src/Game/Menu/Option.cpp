#include "Option.h"
#include "../../Engine/Engine.h"
#include "ControllerPromptPresenter.h"

#include "../GameManager/GameManager.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
bool isFocusableElement(const PElement& element)
{
	return element != nullptr
		&& element->visible
		&& element->activated
		&& element->rect.w > 0
		&& element->rect.h > 0;
}

bool hasButtonImage(const std::shared_ptr<Button>& button)
{
	return button != nullptr
		&& std::any_of(std::begin(button->image), std::end(button->image),
			[](const _shared_imp& image) { return image != nullptr; });
}

bool isPresentedCheckBoxOption(
	const std::shared_ptr<CheckBox>& checkBox,
	const std::shared_ptr<ImageContainer>& labelBackground)
{
	if (!isFocusableElement(checkBox))
	{
		return false;
	}
	return hasButtonImage(checkBox)
		|| (isFocusableElement(labelBackground)
			&& labelBackground->impImage != nullptr);
}

PElement getScrollbarFocusElement(const std::shared_ptr<Scrollbar>& scrollbar)
{
	if (scrollbar == nullptr || scrollbar->max <= scrollbar->min
		|| !isFocusableElement(scrollbar)
		|| !isFocusableElement(scrollbar->slideBtn))
	{
		return nullptr;
	}
	return scrollbar->slideBtn;
}

bool consumeHorizontalNavigation(UIFocusDirection direction)
{
	return direction == UIFocusDirection::Left
		|| direction == UIFocusDirection::Right;
}

FlatTextButtonStyle makeUnavailableCheatActionStyle()
{
	FlatTextButtonStyle style;
	style.normal = { { 92, 92, 92, 155 }, { 35, 35, 35, 225 }, 0xFFAAAAAA };
	style.hovered = { { 140, 120, 88, 185 }, { 50, 45, 40, 230 }, 0xFFD8C9B0 };
	style.pressed = { { 165, 135, 92, 210 }, { 64, 52, 42, 230 }, 0xFFFFFFFF };
	return style;
}
}

Option::Option()
{
	name = "Option";
	canCallBack = true;
	focusManager.setInputAwarePresentation();
	init();
}

Option::~Option()
{
	freeResource();
}

void Option::init()
{
	const std::string preferredFocusId = focusManager.getFocusedNodeId();
	const std::string returnFocusId = mainOptionsFocusId;
	const bool reopenCheatPanel = cheatPanelVisible;
	freeResource();

	loadMenuDefinition("ini\\ui\\option\\option.menu.ini");

	rtnBtn = getComponentByName<Button>("rtnBtn");
	music = getComponentByName<Scrollbar>("music");
	sound = getComponentByName<Scrollbar>("sound");
	speed = getComponentByName<Scrollbar>("speed");
	musicCB = getComponentByName<CheckBox>("musicCB");
	soundCB = getComponentByName<CheckBox>("soundCB");
	speedCB = getComponentByName<CheckBox>("speedCB");
	playerAlpha = getComponentByName<CheckBox>("playerAlpha");
	shadow = getComponentByName<CheckBox>("shadow");
	dyLoad = getComponentByName<CheckBox>("dyLoad");
	playerBg = getComponentByName<ImageContainer>("playerBg");
	shadowBg = getComponentByName<ImageContainer>("shadowBg");
	dyLoadBg = getComponentByName<ImageContainer>("dyLoadBg");

	setChildRectReferToParent();

	if (music && getScrollbarFocusElement(music) != nullptr)
	{
		if (Config::getMusicVolume() <= 0)
		{
			music->setPosition(music->min);
			if (musicCB) musicCB->checked = true;
		}
		else
		{
			int pos = Config::getMusicVolume() > 1.0f ? music->max : (int)(Config::getMusicVolume() * (float)(music->max - music->min) + music->min);
			music->setPosition(pos);
			if (musicCB) musicCB->checked = false;
		}
		musicPos = music->position;
	}
	else
	{
		if (music != nullptr)
		{
			music->activated = false;
			musicPos = music->position;
		}
		if (musicCB != nullptr) musicCB->activated = false;
	}

	if (sound && getScrollbarFocusElement(sound) != nullptr)
	{
		if (Config::getSoundVolume() <= 0)
		{
			sound->setPosition(sound->min);
			if (soundCB) soundCB->checked = true;
		}
		else
		{
			int pos = Config::getSoundVolume() > 1.0f ? sound->max : (int)(Config::getSoundVolume() * (float)(sound->max - sound->min) + sound->min);
			sound->setPosition(pos);
			if (soundCB) soundCB->checked = false;
		}
		soundPos = sound->position;
	}
	else
	{
		if (sound != nullptr)
		{
			sound->activated = false;
			soundPos = sound->position;
		}
		if (soundCB != nullptr) soundCB->activated = false;
	}

	if (playerAlpha)
	{
		playerAlpha->activated = isPresentedCheckBoxOption(playerAlpha, playerBg);
		playerAlpha->checked = !Config::playerAlpha;
	}

	if (speedCB)
	{
		speedCB->activated = true;
		speedCB->checked = false;
	}

	if (speed && getScrollbarFocusElement(speed) != nullptr)
	{
		speed->activated = true;
		speed->setPosition(speedToPos(Config::getGameSpeed()));
		speedPos = speed->position;
		if (speedCB)
		{
			speedCB->checked = speed->position <= speed->min;
		}
	}
	else
	{
		if (speed != nullptr)
		{
			speed->activated = false;
			speedPos = speed->position;
		}
		if (speedCB != nullptr) speedCB->activated = false;
	}

	if (dyLoad)
	{
		dyLoad->activated = isPresentedCheckBoxOption(dyLoad, dyLoadBg);
		dyLoad->checked = !Config::loadAsync;
	}

	if (shadow)
	{
		shadow->activated = false;
		shadow->checked = false;
	}

	createFooterOptions();
	createCheatPanel();
	if (reopenCheatPanel)
	{
		openCheatPanel(preferredFocusId, returnFocusId);
	}
	else
	{
		configureFocus(preferredFocusId);
	}
}

void Option::freeResource()
{
	focusManager.clear();
	touchControlsButton = nullptr;
	cheatSettingsButton = nullptr;
	cheatPanelTitle = nullptr;
	cheatResultLabel = nullptr;
	cheatModeButton = nullptr;
	invincibilityButton = nullptr;
	restoreResourcesButton = nullptr;
	increaseMagicLevelButton = nullptr;
	increasePlayerLevelButton = nullptr;
	addMoneyButton = nullptr;
	cheatPanelBackButton = nullptr;
	mainOptionPresentation.clear();
	mainOptionsFocusId.clear();
	cheatPanelVisible = false;
	rtnBtn = nullptr;
	music = nullptr;
	sound = nullptr;
	speed = nullptr;
	playerAlpha = nullptr;
	dyLoad = nullptr;
	shadow = nullptr;
	playerBg = nullptr;
	dyLoadBg = nullptr;
	shadowBg = nullptr;
	musicCB = nullptr;
	soundCB = nullptr;
	speedCB = nullptr;
	musicPos = 0;
	soundPos = 0;
	speedPos = 0;
	ConfigDrivenPanel::freeResource();
}

void Option::configureFocus(const std::string& preferredFocusId)
{
	focusManager.clear();
	std::vector<UIFocusNodeBinding> focusRows;
	focusRows.reserve(7);
	auto addRow = [&focusRows](
		const std::string& id,
		const PElement& element,
		UIFocusManager::ActionHandler confirm,
		UIFocusManager::NavigationHandler navigate)
	{
		if (!isFocusableElement(element))
		{
			return;
		}
		focusRows.push_back(
		{
			id,
			element,
			std::move(confirm),
			UIFocusManager::ActionHandler(),
			UIFocusManager::ActionHandler(),
			std::move(navigate)
		});
	};
	auto sliderNavigation = [this](
		const std::shared_ptr<Scrollbar>& scrollbar,
		UIFocusDirection direction)
	{
		if (direction == UIFocusDirection::Left)
		{
			return adjustScrollbar(scrollbar, -1);
		}
		if (direction == UIFocusDirection::Right)
		{
			return adjustScrollbar(scrollbar, 1);
		}
		return false;
	};

	addRow("music", getScrollbarFocusElement(music),
		[this]() { toggleMusicMute(); },
		[this, sliderNavigation](UIFocusDirection direction)
		{
			return sliderNavigation(music, direction);
		});
	addRow("sound", getScrollbarFocusElement(sound),
		[this]() { toggleSoundMute(); },
		[this, sliderNavigation](UIFocusDirection direction)
		{
			return sliderNavigation(sound, direction);
		});
	addRow("speed", getScrollbarFocusElement(speed),
		[this]() { resetSpeedToDefault(); },
		[this, sliderNavigation](UIFocusDirection direction)
		{
			return sliderNavigation(speed, direction);
		});
	addRow("player-alpha", playerAlpha,
		[this]() { togglePlayerAlpha(); }, consumeHorizontalNavigation);
	addRow("dynamic-loading", dyLoad,
		[this]() { toggleDynamicLoading(); }, consumeHorizontalNavigation);
	addRow("touch-controls", touchControlsButton,
		[this]() { toggleTouchControlsOption(); },
		[this](UIFocusDirection direction)
		{
			if (consumeHorizontalNavigation(direction)
				&& cheatSettingsButton != nullptr)
			{
				return focusManager.focusNode("cheat-settings");
			}
			return false;
		});
	addRow("cheat-settings", cheatSettingsButton,
		[this]() { openCheatPanel(); },
		[this](UIFocusDirection direction)
		{
			if (consumeHorizontalNavigation(direction)
				&& touchControlsButton != nullptr)
			{
				return focusManager.focusNode("touch-controls");
			}
			return false;
		});

	const std::vector<std::string> focusOrder = focusManager.addLinearGroup(
		"option-rows",
		UIFocusLinearAxis::Vertical,
		focusRows);
	focusManager.applyConfigDrivenFocusNavigation(
		*this,
		{
			{ "music", "music" },
			{ "sound", "sound" },
			{ "speed", "speed" },
			{ "playerAlpha", "player-alpha" },
			{ "dyLoad", "dynamic-loading" },
		});
	if (!focusOrder.empty())
	{
		focusManager.setDefaultFocus(focusOrder.front());
	}
	focusManager.setCancelHandler([this]() { closeMenu(); });
	if (preferredFocusId.empty()
		|| !focusManager.focusNode(preferredFocusId))
	{
		focusManager.focusDefault();
	}
}

void Option::configureCheatPanelFocus(const std::string& preferredFocusId)
{
	focusManager.clear();
	const std::vector<std::string> focusOrder = focusManager.addLinearGroup(
		"cheat-actions",
		UIFocusLinearAxis::Vertical,
		{
			{ "cheat-mode", cheatModeButton,
				[this]() { toggleCheatModeOption(); } },
			{ "invincibility", invincibilityButton,
				[this]() { toggleInvincibilityOption(); } },
			{ "restore-resources", restoreResourcesButton,
				[this]()
				{
					if (gm != nullptr)
					{
						setCheatPanelResult(gm->performCheatAction(
							GameManager::CheatAction::RestorePlayerResources).message);
					}
				} },
			{ "increase-magic-level", increaseMagicLevelButton,
				[this]()
				{
					if (gm != nullptr)
					{
						setCheatPanelResult(gm->performCheatAction(
							GameManager::CheatAction::IncreasePracticeMagicLevel).message);
					}
				} },
			{ "increase-player-level", increasePlayerLevelButton,
				[this]()
				{
					if (gm != nullptr)
					{
						setCheatPanelResult(gm->performCheatAction(
							GameManager::CheatAction::IncreasePlayerLevel).message);
					}
				} },
			{ "add-money", addMoneyButton,
				[this]()
				{
					if (gm != nullptr)
					{
						setCheatPanelResult(gm->performCheatAction(
							GameManager::CheatAction::AddMoney).message);
					}
				} },
			{ "cheat-back", cheatPanelBackButton,
				[this]() { closeCheatPanel(); } }
		});
	if (!focusOrder.empty())
	{
		focusManager.setDefaultFocus(focusOrder.front());
	}
	focusManager.setCancelHandler([this]() { closeCheatPanel(); });
	if (preferredFocusId.empty()
		|| !focusManager.focusNode(preferredFocusId))
	{
		focusManager.focusDefault();
	}
}

void Option::closeMenu()
{
	if (cheatPanelVisible)
	{
		closeCheatPanel();
		return;
	}
	logicRunning = false;
	result = erOK;
}

bool Option::adjustScrollbar(const std::shared_ptr<Scrollbar>& scrollbar, int direction)
{
	if (getScrollbarFocusElement(scrollbar) == nullptr || direction == 0)
	{
		return false;
	}

	const int oldPosition = scrollbar->position;
	const int step = scrollbar->lineSize > 0 ? scrollbar->lineSize : 1;
	scrollbar->setPosition(oldPosition + (direction < 0 ? -step : step));
	if (scrollbar->position == oldPosition)
	{
		return true;
	}

	if (scrollbar == music)
	{
		applyMusicPosition();
	}
	else if (scrollbar == sound)
	{
		applySoundPosition();
	}
	else if (scrollbar == speed)
	{
		applySpeedPosition();
	}
	return true;
}

void Option::applyMusicPosition()
{
	if (getScrollbarFocusElement(music) == nullptr)
	{
		return;
	}
	musicPos = music->position;
	if (musicCB)
	{
		musicCB->checked = musicPos <= music->min;
	}
	float volume = 0.0f;
	if (music->max > music->min)
	{
		volume = (float)(musicPos - music->min) / (float)(music->max - music->min);
	}
	Config::setMusicVolume(volume);
	Config::save();
}

void Option::applySoundPosition()
{
	if (getScrollbarFocusElement(sound) == nullptr)
	{
		return;
	}
	soundPos = sound->position;
	if (soundCB)
	{
		soundCB->checked = soundPos <= sound->min;
	}
	float volume = 0.0f;
	if (sound->max > sound->min)
	{
		volume = (float)(soundPos - sound->min) / (float)(sound->max - sound->min);
	}
	Config::setSoundVolume(volume);
	Config::save();
}

void Option::applySpeedPosition()
{
	if (getScrollbarFocusElement(speed) == nullptr)
	{
		return;
	}
	speedPos = speed->position;
	if (speedCB)
	{
		speedCB->checked = speedPos <= speed->min;
	}
	Config::setGameSpeed(posToSpeed(speedPos));
	Config::save();
}

void Option::applyMusicCheckBox()
{
	if (musicCB == nullptr || music == nullptr)
	{
		return;
	}
	const int requestedPosition = musicCB->checked ? music->min : music->max;
	if (music->position == requestedPosition)
	{
		return;
	}
	music->setPosition(requestedPosition);
	applyMusicPosition();
}

void Option::applySoundCheckBox()
{
	if (soundCB == nullptr || sound == nullptr)
	{
		return;
	}
	const int requestedPosition = soundCB->checked ? sound->min : sound->max;
	if (sound->position == requestedPosition)
	{
		return;
	}
	sound->setPosition(requestedPosition);
	applySoundPosition();
}

void Option::toggleMusicMute()
{
	if (musicCB == nullptr)
	{
		return;
	}
	musicCB->checked = !musicCB->checked;
	applyMusicCheckBox();
}

void Option::toggleSoundMute()
{
	if (soundCB == nullptr)
	{
		return;
	}
	soundCB->checked = !soundCB->checked;
	applySoundCheckBox();
}

void Option::resetSpeedToDefault()
{
	if (getScrollbarFocusElement(speed) == nullptr)
	{
		return;
	}
	const int defaultPosition = speedToPos(SPEED_TIME_DEFAULT);
	if (speedCB)
	{
		speedCB->checked = defaultPosition <= speed->min;
	}
	if (speed->position == defaultPosition)
	{
		return;
	}
	speed->setPosition(defaultPosition);
	applySpeedPosition();
}

void Option::applyPlayerAlpha()
{
	if (playerAlpha == nullptr)
	{
		return;
	}
	Config::playerAlpha = !playerAlpha->checked;
	Config::save();
}

void Option::togglePlayerAlpha()
{
	if (playerAlpha == nullptr)
	{
		return;
	}
	playerAlpha->checked = !playerAlpha->checked;
	applyPlayerAlpha();
}

void Option::applyDynamicLoading()
{
	if (dyLoad == nullptr)
	{
		return;
	}
	Config::loadAsync = !dyLoad->checked;
	Config::save();
}

void Option::toggleDynamicLoading()
{
	if (dyLoad == nullptr)
	{
		return;
	}
	dyLoad->checked = !dyLoad->checked;
	applyDynamicLoading();
}

int Option::speedToPos(float spd)
{
	if (speed == nullptr || speed->max <= speed->min)
	{
		return speed == nullptr ? 0 : speed->min;
	}

	if (spd <= SPEED_TIME_MIN)
	{
		return speed->min;
	}
	else if (spd >= SPEED_TIME_MAX)
	{
		return speed->max;
	}
	return (int)round((spd - SPEED_TIME_MIN) / (SPEED_TIME_MAX - SPEED_TIME_MIN) * (speed->max - speed->min) + speed->min);
}

float Option::posToSpeed(int pos)
{
	if (speed == nullptr || speed->max <= speed->min)
	{
		return SPEED_TIME_DEFAULT;
	}

	if (pos <= speed->min)
	{
		return SPEED_TIME_MIN;
	}
	else if (pos >= speed->max)
	{
		return SPEED_TIME_MAX;
	}
	return ((float)(pos - speed->min)) / (speed->max - speed->min) * (SPEED_TIME_MAX - SPEED_TIME_MIN) + SPEED_TIME_MIN;
}

void Option::onEvent()
{
	if (touchControlsButton == nullptr || cheatSettingsButton == nullptr)
	{
		const bool hadTouchControlsButton = touchControlsButton != nullptr;
		const bool hadCheatSettingsButton = cheatSettingsButton != nullptr;
		createFooterOptions();
		if (!cheatPanelVisible
			&& (hadTouchControlsButton != (touchControlsButton != nullptr)
				|| hadCheatSettingsButton != (cheatSettingsButton != nullptr)))
		{
			configureFocus(focusManager.getFocusedNodeId());
		}
	}
	if (cheatModeButton == nullptr)
	{
		createCheatPanel();
	}
	if (cheatPanelVisible)
	{
		syncCheatPanel();
		return;
	}
	if (getScrollbarFocusElement(music) != nullptr && musicPos != music->position)
	{
		applyMusicPosition();
	}
	if (getScrollbarFocusElement(sound) != nullptr && soundPos != sound->position)
	{
		applySoundPosition();
	}
	if (getScrollbarFocusElement(speed) != nullptr && speedPos != speed->position)
	{
		applySpeedPosition();
	}
	if (musicCB && musicCB->getResult(erClick))
	{
		applyMusicCheckBox();
	}
	if (soundCB && soundCB->getResult(erClick))
	{
		applySoundCheckBox();
	}
	if (speedCB && speedCB->getResult(erClick))
	{
		resetSpeedToDefault();
	}
	if (playerAlpha && playerAlpha->getResult(erClick))
	{
		applyPlayerAlpha();
	}
	if (dyLoad && dyLoad->getResult(erClick))
	{
		applyDynamicLoading();
	}
	if (rtnBtn && rtnBtn->getResult(erClick))
	{
		closeMenu();
		return;
	}
	syncTouchControlsOption();
}

bool Option::onHandleEvent(AEvent & e)
{
	if ((e.eventType == ET_MOUSEDOWN
			&& e.eventData == MBC_MOUSE_LEFT)
		|| e.eventType == ET_FINGERDOWN)
	{
		adoptUIFocusPointerTarget(
			e.eventType == ET_MOUSEDOWN ? TOUCH_MOUSEID : e.eventData);
	}
	if (dispatchKeyboardUIAction(e, *this))
	{
		return true;
	}
	return false;
}

bool Option::onHandleUIAction(UIAction action)
{
	return focusManager.handleAction(action);
}

void Option::onDraw()
{
	ImageContainer::onDraw();
	if (!cheatPanelVisible || engine == nullptr)
	{
		return;
	}
	const Rect panelRect = getCheatPanelRect();
	engine->fillRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h,
		18, 15, 13, 238);
	engine->fillRect(panelRect.x, panelRect.y, panelRect.w, 2,
		196, 154, 83, 255);
	engine->fillRect(panelRect.x, panelRect.y + panelRect.h - 2,
		panelRect.w, 2, 88, 58, 34, 255);
	engine->fillRect(panelRect.x, panelRect.y, 2, panelRect.h,
		158, 116, 66, 255);
	engine->fillRect(panelRect.x + panelRect.w - 2, panelRect.y,
		2, panelRect.h, 88, 58, 34, 255);
}

void Option::onDrawEnd()
{
	if (!visible || engine == nullptr
		|| !Element::isCurrentRunOwner(this)
		|| !focusManager.isFocusPresented())
	{
		return;
	}
	using GameInput::InputAction;
	const std::vector<ControllerPromptItem> items =
	{
		{ InputAction::NavigateUp,
			cheatPanelVisible ? "选择" : "选择/调整" },
		{ InputAction::Confirm,
			cheatPanelVisible ? "执行/切换" : "切换/默认" },
		{ InputAction::Cancel,
			cheatPanelVisible ? "返回选项" : "返回" }
	};
	ControllerPromptPresenter::drawBottomBar(
		engine, engine->inputActions(), items);
}

void Option::onRun()
{
	if (touchControlsButton == nullptr || cheatSettingsButton == nullptr)
	{
		createFooterOptions();
		if (!cheatPanelVisible)
		{
			configureFocus(focusManager.getFocusedNodeId());
		}
	}
	if (cheatModeButton == nullptr)
	{
		createCheatPanel();
	}
	focusManager.focusDefault();
}

void Option::onChildCallBack(PElement child)
{
	if (child == touchControlsButton && child->getResult(erClick))
	{
		toggleTouchControlsOption();
		return;
	}
	if (child == cheatSettingsButton && child->getResult(erClick))
	{
		openCheatPanel();
		return;
	}
	if (child == cheatModeButton && child->getResult(erClick))
	{
		toggleCheatModeOption();
		return;
	}
	if (child == invincibilityButton && child->getResult(erClick))
	{
		toggleInvincibilityOption();
		return;
	}
	if (child == restoreResourcesButton && child->getResult(erClick))
	{
		if (gm != nullptr)
		{
			setCheatPanelResult(gm->performCheatAction(
				GameManager::CheatAction::RestorePlayerResources).message);
		}
		return;
	}
	if (child == increaseMagicLevelButton && child->getResult(erClick))
	{
		if (gm != nullptr)
		{
			setCheatPanelResult(gm->performCheatAction(
				GameManager::CheatAction::IncreasePracticeMagicLevel).message);
		}
		return;
	}
	if (child == increasePlayerLevelButton && child->getResult(erClick))
	{
		if (gm != nullptr)
		{
			setCheatPanelResult(gm->performCheatAction(
				GameManager::CheatAction::IncreasePlayerLevel).message);
		}
		return;
	}
	if (child == addMoneyButton && child->getResult(erClick))
	{
		if (gm != nullptr)
		{
			setCheatPanelResult(gm->performCheatAction(
				GameManager::CheatAction::AddMoney).message);
		}
		return;
	}
	if (child == cheatPanelBackButton && child->getResult(erClick))
	{
		closeCheatPanel();
	}
}

void Option::createFooterOptions()
{
	GameManager* gameManager = GameManager::getInstance();
	if (gameManager == nullptr || rect.w <= 16 || rect.h <= 16)
	{
		return;
	}

	if (gameManager->controller != nullptr && touchControlsButton == nullptr)
	{
		touchControlsButton = std::make_shared<FlatTextButton>();
		touchControlsButton->name = "touchControlsButton";
		touchControlsButton->setFontSize(16);
		addChild(touchControlsButton);
	}
	if (cheatSettingsButton == nullptr)
	{
		cheatSettingsButton = std::make_shared<FlatTextButton>();
		cheatSettingsButton->name = "cheatSettingsButton";
		cheatSettingsButton->setFontSize(16);
		cheatSettingsButton->setUTF8Str("作弊设置");
		addChild(cheatSettingsButton);
	}
	layoutFooterOptions();
	syncTouchControlsOption();
}

void Option::layoutFooterOptions()
{
	std::vector<std::shared_ptr<FlatTextButton>> buttons;
	if (touchControlsButton != nullptr)
	{
		buttons.push_back(touchControlsButton);
	}
	if (cheatSettingsButton != nullptr)
	{
		buttons.push_back(cheatSettingsButton);
	}
	if (buttons.empty())
	{
		return;
	}

	constexpr int ButtonHeight = 30;
	constexpr int ButtonMaximumWidth = 220;
	constexpr int PanelMargin = 8;
	constexpr int ButtonGap = 8;
	const int availableWidth = std::max(1, rect.w - PanelMargin * 2);
	const int totalWidth = buttons.size() > 1
		? std::min(availableWidth, ButtonMaximumWidth * 2 + ButtonGap)
		: std::min(availableWidth, ButtonMaximumWidth);
	const int buttonWidth = buttons.size() > 1
		? std::max(1, (totalWidth - ButtonGap) / 2)
		: totalWidth;
	const int maximumY = rect.y + rect.h - ButtonHeight - PanelMargin;
	const int requestedY = rtnBtn != nullptr
		? rtnBtn->rect.y + rtnBtn->rect.h + PanelMargin
		: maximumY;
	const int buttonY = std::min(requestedY, maximumY);
	int buttonX = rect.x + (rect.w - totalWidth) / 2;
	for (std::size_t index = 0; index < buttons.size(); ++index)
	{
		const int width = index + 1 == buttons.size()
			? rect.x + (rect.w + totalWidth) / 2 - buttonX
			: buttonWidth;
		buttons[index]->rect = { buttonX, buttonY, width, ButtonHeight };
		buttonX += width + ButtonGap;
	}
}

void Option::syncTouchControlsOption()
{
	if (touchControlsButton == nullptr)
	{
		return;
	}
	GameManager* gameManager = GameManager::getInstance();
	if (gameManager == nullptr || gameManager->controller == nullptr)
	{
		touchControlsButton->visible = false;
		touchControlsButton->activated = false;
		return;
	}

	touchControlsButton->visible = true;
	touchControlsButton->activated = true;
	touchControlsButton->setUTF8Str(
		gameManager->controller->areTouchControlsVisible()
			? "显示触控操作区：开"
			: "显示触控操作区：关");
}

void Option::toggleTouchControlsOption()
{
	GameManager* gameManager = GameManager::getInstance();
	if (gameManager == nullptr || gameManager->controller == nullptr)
	{
		return;
	}
	gameManager->requestTouchControlsToggle();
}

void Option::createCheatPanel()
{
	if (cheatModeButton != nullptr || GameManager::getInstance() == nullptr
		|| rect.w <= 16 || rect.h <= 16)
	{
		return;
	}

	cheatPanelTitle = std::make_shared<Label>();
	cheatPanelTitle->name = "cheatPanelTitle";
	cheatPanelTitle->fontSize = 18;
	cheatPanelTitle->minimumFontSize = 14;
	cheatPanelTitle->autoShrink = true;
	cheatPanelTitle->horizontalAlignment = TextHorizontalAlignment::Center;
	cheatPanelTitle->verticalAlignment = TextVerticalAlignment::Center;
	cheatPanelTitle->color = 0xFFFFE7B0;
	cheatPanelTitle->setStr("作弊设置（仅本次运行）");
	addChild(cheatPanelTitle);
	cheatResultLabel = std::make_shared<Label>();
	cheatResultLabel->name = "cheatResultLabel";
	cheatResultLabel->fontSize = 16;
	cheatResultLabel->minimumFontSize = 12;
	cheatResultLabel->autoShrink = true;
	cheatResultLabel->elideOverflow = true;
	cheatResultLabel->horizontalAlignment = TextHorizontalAlignment::Center;
	cheatResultLabel->verticalAlignment = TextVerticalAlignment::Center;
	cheatResultLabel->color = 0xFFFFFFFF;
	addChild(cheatResultLabel);

	auto makeButton = [this](std::shared_ptr<FlatTextButton>& button,
		const std::string& name, const std::string& text)
	{
		button = std::make_shared<FlatTextButton>();
		button->name = name;
		button->setFontSize(16);
		button->setUTF8Str(text);
		addChild(button);
	};
	makeButton(cheatModeButton, "cheatModeButton", "作弊模式：关");
	makeButton(invincibilityButton, "invincibilityButton", "无敌模式：关");
	makeButton(restoreResourcesButton, "restoreResourcesButton",
		"补满生命、内力、体力");
	makeButton(increaseMagicLevelButton, "increaseMagicLevelButton",
		"当前修炼武功提升一级");
	makeButton(increasePlayerLevelButton, "increasePlayerLevelButton",
		"主角提升一级");
	makeButton(addMoneyButton, "addMoneyButton", "增加100000两银子");
	makeButton(cheatPanelBackButton, "cheatPanelBackButton", "返回选项");
	layoutCheatPanel();
	setCheatPanelElementsVisible(false);
}

Rect Option::getCheatPanelRect() const
{
	constexpr int PanelMargin = 8;
	return
	{
		rect.x + PanelMargin,
		rect.y + PanelMargin,
		std::max(1, rect.w - PanelMargin * 2),
		std::max(1, rect.h - PanelMargin * 2)
	};
}

void Option::layoutCheatPanel()
{
	if (cheatPanelTitle == nullptr || cheatModeButton == nullptr)
	{
		return;
	}
	const Rect panelRect = getCheatPanelRect();
	constexpr int ButtonHeight = 32;
	constexpr int ButtonGap = 4;
	constexpr int TitleHeight = 24;
	constexpr int TitleGap = 4;
	constexpr int ResultHeight = 26;
	constexpr int ResultGap = 6;
	constexpr int HorizontalMargin = 12;
	constexpr int ButtonMaximumWidth = 380;
	constexpr int ButtonCount = 7;
	const int contentHeight = TitleHeight + TitleGap + ResultHeight + ResultGap
		+ ButtonCount * ButtonHeight + (ButtonCount - 1) * ButtonGap;
	const int contentY = panelRect.y + std::max(4,
		(panelRect.h - contentHeight) / 2);
	const int buttonWidth = std::min(ButtonMaximumWidth,
		std::max(1, panelRect.w - HorizontalMargin * 2));
	const int buttonX = panelRect.x + (panelRect.w - buttonWidth) / 2;
	cheatPanelTitle->rect =
	{
		buttonX,
		contentY,
		buttonWidth,
		TitleHeight
	};
	cheatResultLabel->rect =
	{
		buttonX,
		contentY + TitleHeight + TitleGap,
		buttonWidth,
		ResultHeight
	};

	const std::vector<std::shared_ptr<FlatTextButton>> buttons =
	{
		cheatModeButton,
		invincibilityButton,
		restoreResourcesButton,
		increaseMagicLevelButton,
		increasePlayerLevelButton,
		addMoneyButton,
		cheatPanelBackButton
	};
	int buttonY = contentY + TitleHeight + TitleGap
		+ ResultHeight + ResultGap;
	for (const auto& button : buttons)
	{
		button->rect = { buttonX, buttonY, buttonWidth, ButtonHeight };
		buttonY += ButtonHeight + ButtonGap;
	}
}

bool Option::isCheatPanelElement(const PElement& element) const
{
	return element == cheatPanelTitle
		|| element == cheatResultLabel
		|| element == cheatModeButton
		|| element == invincibilityButton
		|| element == restoreResourcesButton
		|| element == increaseMagicLevelButton
		|| element == increasePlayerLevelButton
		|| element == addMoneyButton
		|| element == cheatPanelBackButton;
}

void Option::setCheatPanelElementsVisible(bool visibleValue)
{
	const std::vector<PElement> elements =
	{
		cheatPanelTitle,
		cheatResultLabel,
		cheatModeButton,
		invincibilityButton,
		restoreResourcesButton,
		increaseMagicLevelButton,
		increasePlayerLevelButton,
		addMoneyButton,
		cheatPanelBackButton
	};
	for (const PElement& element : elements)
	{
		if (element == nullptr)
		{
			continue;
		}
		element->visible = visibleValue;
		element->activated = visibleValue;
	}
}

void Option::setCheatPanelResult(const std::string& message)
{
	if (cheatResultLabel != nullptr)
	{
		cheatResultLabel->setStr(message);
	}
}

void Option::syncCheatPanel()
{
	GameManager* gameManager = GameManager::getInstance();
	if (gameManager == nullptr || cheatModeButton == nullptr)
	{
		return;
	}
	const bool enabled = gameManager->isCheatModeEnabled();
	cheatModeButton->setUTF8Str(enabled ? "作弊模式：开" : "作弊模式：关");
	if (invincibilityButton != nullptr)
	{
		invincibilityButton->setUTF8Str(
			gameManager->isCheatInvincibilityEnabled()
				? "无敌模式：开"
				: "无敌模式：关");
	}
	const FlatTextButtonStyle actionStyle = enabled
		? FlatTextButtonStyle()
		: makeUnavailableCheatActionStyle();
	for (const auto& button :
		{ invincibilityButton, restoreResourcesButton, increaseMagicLevelButton,
			increasePlayerLevelButton, addMoneyButton })
	{
		if (button != nullptr)
		{
			button->setStyle(actionStyle);
		}
	}
}

void Option::openCheatPanel(
	const std::string& preferredFocusId,
	const std::string& returnFocusId)
{
	if (cheatPanelVisible || cheatModeButton == nullptr)
	{
		return;
	}
	mainOptionsFocusId = returnFocusId.empty()
		? focusManager.getFocusedNodeId()
		: returnFocusId;
	if (mainOptionsFocusId.empty())
	{
		mainOptionsFocusId = "cheat-settings";
	}
	mainOptionPresentation.clear();
	for (const PElement& child : children)
	{
		if (child == nullptr || isCheatPanelElement(child))
		{
			continue;
		}
		mainOptionPresentation.push_back(
			{ child, child->visible, child->activated });
		child->visible = false;
		child->activated = false;
	}
	cheatPanelVisible = true;
	layoutCheatPanel();
	setCheatPanelElementsVisible(true);
	syncCheatPanel();
	setCheatPanelResult(GameManager::getInstance()->isCheatModeEnabled()
		? "作弊模式当前已开启"
		: "作弊模式当前未开启");
	configureCheatPanelFocus(preferredFocusId);
}

void Option::closeCheatPanel()
{
	if (!cheatPanelVisible)
	{
		return;
	}
	focusManager.clear();
	setCheatPanelElementsVisible(false);
	for (const ElementPresentationState& state : mainOptionPresentation)
	{
		if (state.element == nullptr || state.element->parent != this)
		{
			continue;
		}
		state.element->visible = state.visible;
		state.element->activated = state.activated;
	}
	mainOptionPresentation.clear();
	cheatPanelVisible = false;
	const std::string returnFocusId = mainOptionsFocusId;
	mainOptionsFocusId.clear();
	configureFocus(returnFocusId);
}

void Option::toggleCheatModeOption()
{
	GameManager* gameManager = GameManager::getInstance();
	if (gameManager == nullptr)
	{
		return;
	}
	setCheatPanelResult(gameManager->toggleCheatMode().message);
	syncCheatPanel();
}

void Option::toggleInvincibilityOption()
{
	GameManager* gameManager = GameManager::getInstance();
	if (gameManager == nullptr)
	{
		return;
	}
	setCheatPanelResult(gameManager->performCheatAction(
		GameManager::CheatAction::ToggleInvincibility).message);
	syncCheatPanel();
}
