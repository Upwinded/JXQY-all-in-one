#include "Option.h"
#include "../Config/GameSpeedMapping.h"
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
	return hasButtonImage(checkBox) || checkBox->hasText()
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
	touchControlsButton = getComponentByName<FlatTextButton>("touchControlsButton");
	cheatSettingsButton = getComponentByName<FlatTextButton>("cheatSettingsButton");
	themeButton = getComponentByName<FlatTextButton>("themeButton");
	automaticSaveButton = getComponentByName<FlatTextButton>("automaticSaveButton");
	experienceMultiplierButton = getComponentByName<FlatTextButton>("experienceMultiplierButton");
	cheatPanelBackground = getComponentByName<ImageContainer>("cheatPanelBackground");
	cheatPanelTitle = getComponentByName<Label>("cheatPanelTitle");
	cheatResultLabel = getComponentByName<Label>("cheatResultLabel");
	cheatModeButton = getComponentByName<FlatTextButton>("cheatModeButton");
	invincibilityButton = getComponentByName<FlatTextButton>("invincibilityButton");
	restoreResourcesButton = getComponentByName<FlatTextButton>("restoreResourcesButton");
	increaseMagicLevelButton = getComponentByName<FlatTextButton>("increaseMagicLevelButton");
	increasePlayerLevelButton = getComponentByName<FlatTextButton>("increasePlayerLevelButton");
	addMoneyButton = getComponentByName<FlatTextButton>("addMoneyButton");
	cheatPanelBackButton = getComponentByName<FlatTextButton>("cheatPanelBackButton");


	setChildRectReferToParent();
	resourceCheatSettingsButtonRect = cheatSettingsButton != nullptr ? cheatSettingsButton->rect : Rect{};
	for (const auto& definition : componentDefinitions)
	{
		if (!OptionLayout::find(definition.name)) continue;
		auto component = getComponentByName(definition.name);
		if (!component) continue;
		int font = 0;
		if (auto button = std::dynamic_pointer_cast<TextButton>(component)) font = button->getFontSize();
		configuredOptionGeometry[definition.name] = { component->rect, font };
	}


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
	configuredOptionGeometry.clear();
	cheatActionStyles.clear();
	cheatPanelBackground = nullptr;
	controllerPromptTextureCache.itemTextTextures.clear();
	focusManager.clear();
	touchControlsButton = nullptr;
	cheatSettingsButton = nullptr;
	experienceMultiplierButton = nullptr;
	themeButton = nullptr;
	automaticSaveButton = nullptr;
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
			if (direction == UIFocusDirection::Left)
			{
				return focusManager.focusNode("experience-multiplier");
			}
			if (direction == UIFocusDirection::Right)
			{
				return focusManager.focusNode("cheat-settings");
			}
			return false;
		});
	addRow("cheat-settings", cheatSettingsButton,
		[this]() { openCheatPanel(); },
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Left)
			{
				return focusManager.focusNode(touchControlsButton != nullptr
					? "touch-controls" : "experience-multiplier");
			}
			if (direction == UIFocusDirection::Right)
			{
				return focusManager.focusNode("experience-multiplier");
			}
			return false;
		});
	addRow("experience-multiplier", experienceMultiplierButton,
		[this]() { toggleExperienceMultiplierOption(); },
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Left)
			{
				return focusManager.focusNode("cheat-settings");
			}
			if (direction == UIFocusDirection::Right)
			{
				return focusManager.focusNode(touchControlsButton != nullptr
					? "touch-controls" : "cheat-settings");
			}
			return false;
		});

	addRow("ui-theme", themeButton,
		[this]() { toggleThemeOption(); }, consumeHorizontalNavigation);
	addRow("automatic-save", automaticSaveButton,
		[this]() { toggleAutomaticSaveOption(); }, consumeHorizontalNavigation);
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
	speed->setPosition(defaultPosition);
	speedPos = speed->position;
	Config::setGameSpeed(SPEED_TIME_DEFAULT);
	Config::save();
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
	return speed == nullptr ? 0 : GameSpeedMapping::toPosition(spd, speed->min, speed->max);
}

float Option::posToSpeed(int pos)
{
	return speed == nullptr ? SPEED_TIME_DEFAULT : GameSpeedMapping::fromPosition(pos, speed->min, speed->max);
}

void Option::onEvent()
{
	if (touchControlsButton == nullptr || cheatSettingsButton == nullptr || experienceMultiplierButton == nullptr)
	{
		const bool hadTouchControlsButton = touchControlsButton != nullptr;
		const bool hadCheatSettingsButton = cheatSettingsButton != nullptr;
		const bool hadExperienceMultiplierButton = experienceMultiplierButton != nullptr;
		createFooterOptions();
		if (!cheatPanelVisible
			&& (hadTouchControlsButton != (touchControlsButton != nullptr)
				|| hadCheatSettingsButton != (cheatSettingsButton != nullptr)
				|| hadExperienceMultiplierButton != (experienceMultiplierButton != nullptr)))
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
	if (cheatPanelBackground && cheatPanelBackground->drawImagetoRect(panelRect, cheatPanelBackground->stretch)) return;
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
	auto options = ControllerPromptPresenter::bottomBarOptions(engine);
	if (!cheatPanelVisible)
	{
		const auto promptLayout = ControllerPromptPresenter::layout(
			items, ControllerPromptPresenter::captureTheme(engine->inputActions()), options);
		const int promptHeight = std::min(options.height,
			promptLayout.contentHeight + options.verticalPadding * 2);
		const Rect promptRect = { options.x, options.y + (options.height - promptHeight) / 2,
			options.width, promptHeight };
		for (const auto& button : { touchControlsButton, cheatSettingsButton,
			experienceMultiplierButton, themeButton, automaticSaveButton })
		{
			if (button != nullptr && button->visible
				&& button->rect.x < promptRect.x + promptRect.w
				&& button->rect.x + button->rect.w > promptRect.x
				&& button->rect.y < promptRect.y + promptRect.h
				&& button->rect.y + button->rect.h > promptRect.y)
			{
				// Full-height original skins leave space above their first setting row.
				options.y = std::max(8, rect.y - promptHeight - 8);
				options.height = promptHeight;
				break;
			}
		}
	}
	ControllerPromptPresenter::draw(
		engine, engine->inputActions(), items, options, controllerPromptTextureCache);
}

void Option::onRun()
{
	if (touchControlsButton == nullptr || cheatSettingsButton == nullptr || experienceMultiplierButton == nullptr)
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
	if (child == rtnBtn && child->getResult(erClick))
	{
		closeMenu();
		return;
	}
	if (child == automaticSaveButton && child->getResult(erClick))
	{
		toggleAutomaticSaveOption();
		return;
	}
	if (child == themeButton && child->getResult(erClick))
	{
		toggleThemeOption();
		return;
	}
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
	if (child == experienceMultiplierButton && child->getResult(erClick))
	{
		toggleExperienceMultiplierOption();
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
	if (experienceMultiplierButton == nullptr)
	{
		experienceMultiplierButton = std::make_shared<FlatTextButton>();
		experienceMultiplierButton->name = "experienceMultiplierButton";
		experienceMultiplierButton->setStyle(cheatSettingsButton->getStyle());
		experienceMultiplierButton->setFontSize(cheatSettingsButton->getFontSize());
		experienceMultiplierButton->stretch = cheatSettingsButton->stretch;
		std::copy(std::begin(cheatSettingsButton->image), std::end(cheatSettingsButton->image),
			std::begin(experienceMultiplierButton->image));
		addChild(experienceMultiplierButton);
	}
	experienceMultiplierButton->setUTF8Str(u8"经验×" + std::to_string(Config::experienceMultiplier));
	syncTouchControlsOption();
	if (themeButton == nullptr
		&& File::fileExist("ini\\ui\\qingyu\\theme.ini")
		&& File::fileExist("asf\\ui\\qingyu\\panel.png"))
	{
		themeButton = std::make_shared<FlatTextButton>();
		themeButton->name = "themeButton";
		themeButton->setFontSize(16);
		addChild(themeButton);
	}
	if (themeButton)
	{
		themeButton->setUTF8Str(gameManager->global.feature.automaticSave
			? (Config::useQingyuUi ? u8"界面：青玉·重进生效" : u8"界面：原版·重进生效")
			: (Config::useQingyuUi ? u8"界面：青玉 · 重进游戏生效" : u8"界面：原版 · 重进游戏生效"));
	}
	if (gameManager->global.feature.automaticSave && automaticSaveButton == nullptr)
	{
		automaticSaveButton = std::make_shared<FlatTextButton>();
		automaticSaveButton->name = "automaticSaveButton";
		addChild(automaticSaveButton);
	}
	if (automaticSaveButton)
	{
		automaticSaveButton->visible = automaticSaveButton->activated = gameManager->global.feature.automaticSave;
		automaticSaveButton->setUTF8Str(Config::automaticSaveEnabled ? u8"自动存档：开" : u8"自动存档：关");
		if (!gameManager->global.feature.automaticSave) automaticSaveButton.reset();
	}
	layoutFooterOptions();

}

void Option::toggleExperienceMultiplierOption()
{
	Config::experienceMultiplier = Config::experienceMultiplier % 3 + 1;
	Config::save();
	if (experienceMultiplierButton != nullptr)
	{
		experienceMultiplierButton->setUTF8Str(u8"经验×" + std::to_string(Config::experienceMultiplier));
	}
}

void Option::toggleThemeOption()
{
	Config::useQingyuUi = !Config::useQingyuUi;
	Config::save();
	if (themeButton != nullptr)
	{
		themeButton->setUTF8Str(automaticSaveButton
			? (Config::useQingyuUi ? u8"界面：青玉·重进生效" : u8"界面：原版·重进生效")
			: (Config::useQingyuUi ? u8"界面：青玉 · 重进游戏生效" : u8"界面：原版 · 重进游戏生效"));
	}
}

bool Option::hasExplicitOptionLayout(const std::string& name) const
{
	for (const auto& definition : componentDefinitions)
		if (definition.name == name) return definition.optionLayout == OptionLayout::Explicit
			|| definition.optionLayout == OptionLayout::ResourceFooter;
	return false;
}

void Option::layoutFooterOptions()
{
	int width = 0, height = 0;
	engine->getWindowSize(width, height);
	bool explicitFooter = false;
	for (const auto& definition : componentDefinitions)
		if (definition.name == "cheatSettingsButton") explicitFooter = definition.optionLayout == OptionLayout::Explicit;
	const bool legacy = getComponentByName("cheatSettingsButton") != nullptr && !explicitFooter;
	const auto layout = OptionLayout::footer(rect, rtnBtn ? &rtnBtn->rect : nullptr, height,
		touchControlsButton != nullptr, themeButton != nullptr, automaticSaveButton != nullptr,
		legacy ? &resourceCheatSettingsButtonRect : nullptr);
	const std::vector<std::pair<std::string, std::shared_ptr<FlatTextButton>>> buttons = {
		{ "touchControlsButton", touchControlsButton }, { "cheatSettingsButton", cheatSettingsButton },
		{ "experienceMultiplierButton", experienceMultiplierButton }, { "themeButton", themeButton },
		{ "automaticSaveButton", automaticSaveButton }
	};
	for (const auto& entry : buttons)
	{
		if (!entry.second) continue;
		const auto geometry = layout.controls.find(entry.first);
		if (geometry != layout.controls.end())
		{
			entry.second->rect = geometry->second.rectangle;
			if (geometry->second.font > 0) entry.second->setFontSize(geometry->second.font);
		}
		const auto configured = configuredOptionGeometry.find(entry.first);
		if (configured != configuredOptionGeometry.end() && hasExplicitOptionLayout(entry.first))
		{
			entry.second->rect = configured->second.rectangle;
			if (configured->second.font > 0) entry.second->setFontSize(configured->second.font);
		}
	}
	rect.h = layout.windowHeight;
}

void Option::toggleAutomaticSaveOption()
{
	if (auto* gameManager = GameManager::getInstance())
	{
		gameManager->setAutomaticSaveEnabled(!Config::automaticSaveEnabled);
		automaticSaveButton->setUTF8Str(Config::automaticSaveEnabled ? u8"自动存档：开" : u8"自动存档：关");
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
	if (GameManager::getInstance() == nullptr || rect.w <= 16 || rect.h <= 16) return;
	const auto makeLabel = [this](std::shared_ptr<Label>& label, const char* name)
	{
		if (label) return;
		const auto* defaults = OptionLayout::find(name);
		label = std::make_shared<Label>();
		label->name = name;
		label->fontSize = defaults->font;
		label->minimumFontSize = std::string(name) == "cheatPanelTitle" ? 14 : 12;
		label->autoShrink = true;
		label->elideOverflow = std::string(name) == "cheatResultLabel";
		label->horizontalAlignment = TextHorizontalAlignment::Center;
		label->verticalAlignment = TextVerticalAlignment::Center;
		label->color = defaults->color;
		if (!defaults->dynamicText) label->setStr(defaults->text);
		addChild(label);
	};
	makeLabel(cheatPanelTitle, "cheatPanelTitle");
	makeLabel(cheatResultLabel, "cheatResultLabel");
	const auto makeButton = [this](std::shared_ptr<FlatTextButton>& button, const char* name)
	{
		if (button) return;
		const auto* defaults = OptionLayout::find(name);
		button = std::make_shared<FlatTextButton>();
		button->name = name;
		button->setFontSize(defaults->font);
		button->setUTF8Str(defaults->text);
		addChild(button);
	};
	makeButton(cheatModeButton, "cheatModeButton");
	makeButton(invincibilityButton, "invincibilityButton");
	makeButton(restoreResourcesButton, "restoreResourcesButton");
	makeButton(increaseMagicLevelButton, "increaseMagicLevelButton");
	makeButton(increasePlayerLevelButton, "increasePlayerLevelButton");
	makeButton(addMoneyButton, "addMoneyButton");
	makeButton(cheatPanelBackButton, "cheatPanelBackButton");
	if (cheatPanelBackground) cheatPanelBackground->visible = cheatPanelBackground->activated = false;
	for (const auto& entry : std::vector<std::pair<std::string, std::shared_ptr<FlatTextButton>>>{
		{ "invincibilityButton", invincibilityButton }, { "restoreResourcesButton", restoreResourcesButton },
		{ "increaseMagicLevelButton", increaseMagicLevelButton }, { "increasePlayerLevelButton", increasePlayerLevelButton },
		{ "addMoneyButton", addMoneyButton } })
		cheatActionStyles[entry.first] = entry.second->getStyle();
	layoutCheatPanel();
	setCheatPanelElementsVisible(false);
}

Rect Option::getCheatPanelRect() const
{
	const auto configured = configuredOptionGeometry.find("cheatPanelBackground");
	if (configured != configuredOptionGeometry.end() && hasExplicitOptionLayout("cheatPanelBackground"))
		return configured->second.rectangle;
	return OptionLayout::cheatBounds(rect);
}

void Option::layoutCheatPanel()
{
	if (!cheatPanelTitle || !cheatModeButton) return;
	const auto defaults = OptionLayout::cheat(getCheatPanelRect());
	for (const auto& entry : std::vector<std::pair<std::string, PElement>>{
		{ "cheatPanelTitle", cheatPanelTitle }, { "cheatResultLabel", cheatResultLabel },
		{ "cheatModeButton", cheatModeButton }, { "invincibilityButton", invincibilityButton },
		{ "restoreResourcesButton", restoreResourcesButton }, { "increaseMagicLevelButton", increaseMagicLevelButton },
		{ "increasePlayerLevelButton", increasePlayerLevelButton }, { "addMoneyButton", addMoneyButton },
		{ "cheatPanelBackButton", cheatPanelBackButton } })
	{
		const auto configured = configuredOptionGeometry.find(entry.first);
		entry.second->rect = configured != configuredOptionGeometry.end() && hasExplicitOptionLayout(entry.first)
			? configured->second.rectangle : defaults.at(entry.first).rectangle;
	}
}

bool Option::isCheatPanelElement(const PElement& element) const
{
	return element == cheatPanelBackground
		|| element == cheatPanelTitle
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
	for (const auto& entry : std::vector<std::pair<std::string, std::shared_ptr<FlatTextButton>>>{
		{ "invincibilityButton", invincibilityButton }, { "restoreResourcesButton", restoreResourcesButton },
		{ "increaseMagicLevelButton", increaseMagicLevelButton }, { "increasePlayerLevelButton", increasePlayerLevelButton },
		{ "addMoneyButton", addMoneyButton } })
	{
		if (entry.second) entry.second->setStyle(enabled ? cheatActionStyles.at(entry.first) : makeUnavailableCheatActionStyle());
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
