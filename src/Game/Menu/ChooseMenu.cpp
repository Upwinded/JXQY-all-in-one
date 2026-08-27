#include "ChooseMenu.h"
#include "../../Engine/Engine.h"

#include "../../Component/TextLayout.h"
#include "../GameManager/GameManager.h"
#include "../GameTypes.h"

#include <algorithm>

namespace
{
Rect toRect(const ChooseMenuLayoutRectangle& rectangle)
{
	return { rectangle.x, rectangle.y, rectangle.width, rectangle.height };
}

ChooseMenuLayoutRectangle toLayoutRectangle(const Rect& rectangle)
{
	return { rectangle.x, rectangle.y, rectangle.w, rectangle.h };
}
}

ChooseMenu::ChooseMenu()
{
	name = "ChooseMenu";
	visible = false;
	setPriority(epMax);
	focusManager.setInputAwarePresentation();
	init();
}

ChooseMenu::~ChooseMenu()
{
	freeResource();
}

void ChooseMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\choose\\choose.menu.ini");

	messageLabel = getComponentByName<Label>("messageLabel");
	selectA = getComponentByName<ChooseTextButton>("selectA");
	selectB = getComponentByName<ChooseTextButton>("selectB");

	if (messageLabel != nullptr)
	{
		messageLabel->autoNextLine = true;
		messageLabel->coverMouse = false;
	}
	setChildRectReferToParent();

	preferredPanelRect = rect;
	if (messageLabel != nullptr)
	{
		preferredMessageRect = messageLabel->rect;
		preferredMessageRect.x -= rect.x;
		preferredMessageRect.y -= rect.y;
	}
	if (selectA != nullptr)
	{
		preferredOptionRect = selectA->rect;
		preferredOptionRect.x -= rect.x;
		preferredOptionRect.y -= rect.y;
	}
	if (selectA != nullptr && selectB != nullptr)
	{
		preferredOptionRowGap = std::max(0, selectB->rect.y - selectA->rect.y - selectA->rect.h);
	}

	speakerLabel = std::make_shared<Label>();
	speakerLabel->name = "choosePlusSpeaker";
	speakerLabel->autoNextLine = false;
	speakerLabel->coverMouse = false;
	if (messageLabel != nullptr)
	{
		speakerLabel->fontSize = messageLabel->fontSize;
		speakerLabel->color = messageLabel->color;
	}
	addChild(speakerLabel);

	portraitImage = std::make_shared<ImageContainer>();
	portraitImage->name = "choosePlusPortrait";
	portraitImage->stretch = true;
	portraitImage->keepAspect = true;
	portraitImage->coverMouse = false;
	addChild(portraitImage);

	stretch = true;
	applyChoiceState();
}

void ChooseMenu::choose(const std::string& message, const std::string& optionA, const std::string& optionB)
{
	chooseOptions(message, { optionA, optionB }, { true, true }, true);
}

void ChooseMenu::chooseEx(const std::string& message, const std::vector<std::string>& options, const std::vector<bool>& visibleOptions)
{
	chooseOptions(message, options, visibleOptions, false);
}

void ChooseMenu::chooseOptions(
	const std::string& message,
	const std::vector<std::string>& options,
	const std::vector<bool>& visibleOptions,
	bool alignToDialog)
{
	SelectionConfiguration configuration;
	configuration.message = message;
	configuration.options = options;
	configuration.visibleOptions = visibleOptions;
	configuration.alignToDialog = alignToDialog;
	if (!prepareSelection(configuration))
	{
		return;
	}

	run();
	visible = false;
}

void ChooseMenu::choosePlus(
	const std::string& speakerName,
	const std::string& portraitFileName,
	int dialogPosition,
	const std::string& message,
	const std::vector<std::string>& options,
	const std::vector<bool>& visibleOptions)
{
	SelectionConfiguration configuration;
	configuration.message = message;
	configuration.options = options;
	configuration.visibleOptions = visibleOptions;
	configuration.choosePlus = true;
	configuration.speakerName = speakerName;
	configuration.portraitFileName = portraitFileName;
	configuration.dialogPosition = dialogPosition;
	if (!prepareSelection(configuration))
	{
		return;
	}

	run();
	visible = false;
}

void ChooseMenu::chooseMultiple(
	const std::string& message,
	const std::vector<std::string>& options,
	const std::vector<bool>& visibleOptions,
	int columnCount,
	int selectionCount)
{
	SelectionConfiguration configuration;
	configuration.message = message;
	configuration.options = options;
	configuration.visibleOptions = visibleOptions;
	configuration.multiple = true;
	configuration.columnCount = columnCount;
	configuration.selectionCount = selectionCount;
	if (!prepareSelection(configuration))
	{
		return;
	}

	run();
	visible = false;
}

int ChooseMenu::getSelection()
{
	return selection;
}

std::vector<int> ChooseMenu::getMultipleSelection() const
{
	return multipleSelection.selections();
}

void ChooseMenu::onEvent()
{
	if (!visible || !isInSelecting)
	{
		return;
	}

	if (previousPageButton != nullptr && previousPageButton->visible && previousPageButton->getResult(erClick))
	{
		changePage(-1);
		return;
	}
	if (nextPageButton != nullptr && nextPageButton->visible && nextPageButton->getResult(erClick))
	{
		changePage(1);
		return;
	}

	for (auto& button : choiceButtons)
	{
		if (button != nullptr && button->visible && button->getResult(erClick))
		{
			selectChoice(button->index);
			return;
		}
	}

	if (multipleSelectionMode)
	{
		if (multipleConfirmButton != nullptr && multipleConfirmButton->visible && multipleConfirmButton->getResult(erClick))
		{
			finishMultipleSelection();
			return;
		}
		if (multipleClearButton != nullptr && multipleClearButton->visible && multipleClearButton->getResult(erClick))
		{
			clearMultipleSelection();
		}
	}
}

bool ChooseMenu::onHandleEvent(AEvent& event)
{
	if (!visible || !isInSelecting)
	{
		return false;
	}
	if (isPointerTakeoverEvent(event))
	{
		updateKeyboardNavigationIndicator(false);
	}
	if ((event.eventType == ET_MOUSEDOWN
			&& event.eventData == MBC_MOUSE_LEFT)
		|| event.eventType == ET_FINGERDOWN)
	{
		adoptUIFocusPointerTarget(
			event.eventType == ET_MOUSEDOWN
				? TOUCH_MOUSEID : event.eventData);
	}

	if (event.eventType == ET_MOUSEWHEEL && currentPageCount > 1 && event.eventData != 0)
	{
		changePage(event.eventData > 0 ? 1 : -1);
		return true;
	}
	if (event.eventType != ET_KEYDOWN)
	{
		return false;
	}

	if (event.eventData == KEY_ESCAPE)
	{
		return true;
	}
	if (event.eventData == KEY_LEFT || event.eventData == KEY_UP
		|| event.eventData == KEY_A || event.eventData == KEY_W
		|| event.eventData == KEY_RIGHT || event.eventData == KEY_DOWN
		|| event.eventData == KEY_D || event.eventData == KEY_S
		|| event.eventData == KEY_RETURN || event.eventData == KEY_SPACE)
	{
		// Keyboard navigation shares the same concrete focus graph as gamepad
		// navigation. Pagination remains available through the page-row controls
		// and the existing wheel/panel actions.
		// Repeated confirm keys and directions at a focus boundary remain owned
		// by this modal choice. The mapper suppresses repeated confirmation, but
		// the visible keyboard cursor must remain on the current option.
		dispatchKeyboardUIAction(event, *this);
		updateKeyboardNavigationIndicator(visible && isInSelecting);
		return true;
	}

	int pageSelectionIndex = -1;
	if (event.eventData >= KEY_1 && event.eventData <= KEY_9)
	{
		pageSelectionIndex = static_cast<int>(event.eventData - KEY_1);
	}
	else if (event.eventData == KEY_0)
	{
		pageSelectionIndex = 9;
	}
	if (pageSelectionIndex >= 0 && static_cast<size_t>(pageSelectionIndex) < choiceButtons.size())
	{
		updateKeyboardNavigationIndicator(false);
		selectChoice(choiceButtons[pageSelectionIndex]->index);
		return true;
	}
	return false;
}

bool ChooseMenu::onHandleUIAction(UIAction action)
{
	if (!visible || !isInSelecting)
	{
		return false;
	}
	// Direct semantic actions are gamepad/test entry points. Keyboard dispatch
	// restores its independent indicator after this call returns.
	updateKeyboardNavigationIndicator(false);
	return focusManager.handleAction(action);
}

void ChooseMenu::freeResource()
{
	updateKeyboardNavigationIndicator(false);
	focusManager.clear();
	messageLabel = nullptr;
	speakerLabel = nullptr;
	portraitImage = nullptr;
	selectA = nullptr;
	selectB = nullptr;
	previousPageButton = nullptr;
	nextPageButton = nullptr;
	multipleConfirmButton = nullptr;
	multipleClearButton = nullptr;
	choiceButtons.clear();
	currentMessage.clear();
	currentOptionA.clear();
	currentOptionB.clear();
	currentOptions.clear();
	currentVisibleOptions.clear();
	currentSpeakerName.clear();
	currentPortraitFileName.clear();
	currentDialogPosition = 2;
	currentPageIndex = 0;
	currentPageCount = 1;
	choosePlusPresentation = false;
	dialogAlignedPresentation = false;
	multipleSelection.reset(0);
	ConfigDrivenPanel::freeResource();
}

bool ChooseMenu::isChoiceOptionVisible(size_t optionIndex) const
{
	if (optionIndex >= currentOptions.size() || currentOptions[optionIndex].empty())
	{
		return false;
	}
	return optionIndex >= currentVisibleOptions.size() || currentVisibleOptions[optionIndex];
}

int ChooseMenu::getVisibleOptionCount() const
{
	int count = 0;
	for (size_t i = 0; i < currentOptions.size(); i++)
	{
		if (isChoiceOptionVisible(i))
		{
			count++;
		}
	}
	return count;
}

bool ChooseMenu::isMultipleOptionSelected(int optionIndex) const
{
	return multipleSelection.contains(optionIndex);
}

void ChooseMenu::toggleMultipleOption(int optionIndex)
{
	multipleSelection.toggle(optionIndex);
}

void ChooseMenu::finishMultipleSelection()
{
	if (multipleSelection.confirm())
	{
		updateKeyboardNavigationIndicator(false);
		focusManager.clear();
		logicRunning = false;
		isInSelecting = false;
		visible = false;
	}
}

void ChooseMenu::clearMultipleSelection()
{
	multipleSelection.clear();
	applyChoiceState();
}

void ChooseMenu::resetPresentation()
{
	updateKeyboardNavigationIndicator(false);
	choosePlusPresentation = false;
	dialogAlignedPresentation = false;
	currentSpeakerName.clear();
	currentPortraitFileName.clear();
	currentDialogPosition = 2;
}

void ChooseMenu::updateKeyboardNavigationIndicator(bool visible)
{
	auto clearIndicator =
		[](const std::shared_ptr<ChooseTextButton>& button)
		{
			if (button != nullptr)
			{
				button->setNavigationHighlighted(false);
			}
		};
	for (const auto& button : choiceButtons)
	{
		clearIndicator(button);
	}
	clearIndicator(selectA);
	clearIndicator(selectB);
	clearIndicator(previousPageButton);
	clearIndicator(nextPageButton);
	clearIndicator(multipleConfirmButton);
	clearIndicator(multipleClearButton);

	keyboardNavigationIndicatorVisible = visible;
	if (!visible)
	{
		return;
	}
	auto focusedButton = std::dynamic_pointer_cast<ChooseTextButton>(
		focusManager.getFocusedElement());
	if (focusedButton == nullptr)
	{
		keyboardNavigationIndicatorVisible = false;
		return;
	}
	focusedButton->setNavigationHighlighted(true);
}

bool ChooseMenu::prepareSelection(const SelectionConfiguration& configuration)
{
	isInSelecting = true;
	multipleSelectionMode = configuration.multiple;
	resetPresentation();
	choosePlusPresentation = configuration.choosePlus;
	dialogAlignedPresentation = configuration.alignToDialog;
	selection = -1;
	multipleSelection.reset(configuration.multiple
		? configuration.selectionCount : 0);
	multipleColumnCount = std::max(1, configuration.columnCount);
	currentPageIndex = 0;
	visible = true;
	currentMessage = configuration.message;
	currentOptionA = configuration.options.size() > 0
		? configuration.options[0] : "";
	currentOptionB = configuration.options.size() > 1
		? configuration.options[1] : "";
	currentOptions = configuration.options;
	currentVisibleOptions = configuration.visibleOptions;
	currentSpeakerName = configuration.speakerName;
	currentPortraitFileName = configuration.portraitFileName;
	currentDialogPosition = configuration.dialogPosition;
	applyChoiceState();

	const bool validSelection = multipleSelectionMode
		? multipleSelection.canSatisfyWithAvailableOptions(getVisibleOptionCount())
		: getVisibleOptionCount() > 0;
	if (!validSelection)
	{
		isInSelecting = false;
		visible = false;
	}
	return validSelection;
}

void ChooseMenu::selectChoice(int optionIndex)
{
	if (multipleSelectionMode)
	{
		toggleMultipleOption(optionIndex);
		applyChoiceState();
		return;
	}

	focusManager.clear();
	logicRunning = false;
	isInSelecting = false;
	selection = optionIndex;
	visible = false;
}

void ChooseMenu::changePage(int offset)
{
	int newPageIndex = std::max(0, std::min(currentPageIndex + offset, currentPageCount - 1));
	if (newPageIndex != currentPageIndex)
	{
		cancelPointerInteraction();
		currentPageIndex = newPageIndex;
		applyChoiceState();
	}
}

void ChooseMenu::applyChoiceState()
{
	updateChoiceButtons();
}

std::shared_ptr<ChooseTextButton> ChooseMenu::getOrCreateChoiceButton(size_t displayIndex)
{
	if (displayIndex == 0)
	{
		return selectA;
	}
	if (displayIndex == 1)
	{
		return selectB;
	}

	auto button = std::make_shared<ChooseTextButton>();
	button->initFromIniFileName("ini\\ui\\choose\\btnA.ini");
	addChild(button);
	return button;
}

std::shared_ptr<ChooseTextButton> ChooseMenu::getOrCreateMultipleControlButton(
	std::shared_ptr<ChooseTextButton>& button,
	const std::string& text)
{
	if (button == nullptr)
	{
		button = std::make_shared<ChooseTextButton>();
		button->initFromIniFileName("ini\\ui\\choose\\btnA.ini");
		addChild(button);
	}
	button->index = -1;
	button->setStr(text);
	return button;
}

std::shared_ptr<ChooseTextButton> ChooseMenu::getOrCreatePageButton(std::shared_ptr<ChooseTextButton>& button)
{
	if (button == nullptr)
	{
		button = std::make_shared<ChooseTextButton>();
		button->initFromIniFileName("ini\\ui\\choose\\btnA.ini");
		addChild(button);
	}
	button->index = -1;
	return button;
}

void ChooseMenu::configureFocus(const std::string& preferredFocusId)
{
	focusManager.clear();
	std::string defaultFocusId;
	std::vector<std::string> choiceIds;
	const int choiceColumnCount = multipleSelectionMode
		? std::max(1, multipleColumnCount) : 1;
	int visibleChoiceIndex = 0;
	for (const auto& button : choiceButtons)
	{
		if (button == nullptr || !button->visible || !button->activated)
		{
			continue;
		}
		const int optionIndex = button->index;
		const std::string focusId = "choice-" + std::to_string(optionIndex);
		focusManager.addNode(focusId, button,
			{ "choice-options",
				visibleChoiceIndex / choiceColumnCount,
				visibleChoiceIndex % choiceColumnCount },
			[this, optionIndex]() { selectChoice(optionIndex); });
		choiceIds.push_back(focusId);
		visibleChoiceIndex++;
		if (defaultFocusId.empty())
		{
			defaultFocusId = focusId;
		}
	}

	auto focusableElement = [](const PElement& element)
	{
		return element != nullptr && element->visible && element->activated
			? element : PElement();
	};
	const std::vector<std::string> pageRowIds = focusManager.addLinearGroup(
		"choice-page-navigation",
		UIFocusLinearAxis::Horizontal,
		{
			{
				"previous-page",
				focusableElement(previousPageButton),
				[this]() { changePage(-1); }
			},
			{
				"next-page",
				focusableElement(nextPageButton),
				[this]() { changePage(1); }
			}
		},
		false);
	const std::vector<std::string> selectionRowIds = focusManager.addLinearGroup(
		"choice-selection-actions",
		UIFocusLinearAxis::Horizontal,
		{
			{
				"clear",
				focusableElement(multipleClearButton),
				[this]() { clearMultipleSelection(); }
			},
			{
				"confirm",
				focusableElement(multipleConfirmButton),
				[this]() { finishMultipleSelection(); }
			}
		},
		false);

	std::vector<std::string> bottomChoiceRowIds;
	if (!choiceIds.empty())
	{
		const std::size_t bottomRowStart =
			((choiceIds.size() - 1) / static_cast<std::size_t>(choiceColumnCount))
			* static_cast<std::size_t>(choiceColumnCount);
		bottomChoiceRowIds.assign(
			choiceIds.begin() + bottomRowStart,
			choiceIds.end());
	}
	const std::vector<std::string>& firstFooterRowIds = pageRowIds.empty()
		? selectionRowIds : pageRowIds;
	focusManager.connectAdjacentRows(bottomChoiceRowIds, firstFooterRowIds);
	focusManager.connectAdjacentRows(pageRowIds, selectionRowIds);

	if (!defaultFocusId.empty())
	{
		focusManager.setDefaultFocus(defaultFocusId);
	}
	// Script choices intentionally cannot be cancelled by the generic B/Escape
	// action; the script must resolve a valid option.
	focusManager.setCancelHandler([]() {});
	focusManager.setPagePreviousHandler([this]() { changePage(-1); });
	focusManager.setPageNextHandler([this]() { changePage(1); });
	focusManager.setPanelPreviousHandler([this]() { changePage(-1); });
	focusManager.setPanelNextHandler([this]() { changePage(1); });
	if (preferredFocusId.empty() || !focusManager.focusNode(preferredFocusId))
	{
		focusManager.focusDefault();
	}
	updateKeyboardNavigationIndicator(keyboardNavigationIndicatorVisible);
}

void ChooseMenu::updateChoiceButtons()
{
	const std::string previousFocusId = focusManager.getFocusedNodeId();
	focusManager.clear();
	for (size_t i = 2; i < choiceButtons.size(); i++)
	{
		removeChild(choiceButtons[i]);
	}
	choiceButtons.clear();
	auto hideButton = [](const std::shared_ptr<ChooseTextButton>& button)
	{
		if (button != nullptr)
		{
			button->visible = false;
			button->activated = false;
			button->setSelected(false);
		}
	};
	hideButton(selectA);
	hideButton(selectB);
	hideButton(previousPageButton);
	hideButton(nextPageButton);
	hideButton(multipleConfirmButton);
	hideButton(multipleClearButton);

	ChooseMenuLayoutInput layoutInput;
	engine->getWindowSize(layoutInput.viewportWidth, layoutInput.viewportHeight);
	layoutInput.preferredPanel = toLayoutRectangle(preferredPanelRect);
	layoutInput.preferredMessage = toLayoutRectangle(preferredMessageRect);
	layoutInput.preferredOption = toLayoutRectangle(preferredOptionRect);
	layoutInput.messageFontSize = messageLabel != nullptr ? messageLabel->fontSize : 14;
	layoutInput.optionFontSize = selectA != nullptr ? selectA->getFontSize() : 14;
	layoutInput.speakerFontSize = speakerLabel != nullptr ? speakerLabel->fontSize : layoutInput.messageFontSize;
	layoutInput.rowGap = preferredOptionRowGap;
	layoutInput.columnGap = 10;
	layoutInput.columnCount = multipleSelectionMode ? multipleColumnCount : 1;
	layoutInput.requestedPageIndex = currentPageIndex;
	GameManager* gameManager = GameManager::getInstance();
	const bool useYycsDialogAlignment = dialogAlignedPresentation
		&& gameManager != nullptr
		&& gameManager->global.feature.menuResourceProfile == mrpYycs;
	layoutInput.anchorPanelToPreferredTop = useYycsDialogAlignment;
	layoutInput.compactDialogAlignedTwoChoice = useYycsDialogAlignment;
	layoutInput.multipleFooter = multipleSelectionMode;
	layoutInput.showSpeaker = choosePlusPresentation
		&& (currentDialogPosition == 0 || currentDialogPosition == 1)
		&& !currentSpeakerName.empty();
	layoutInput.speakerRight = currentDialogPosition == 1;
	layoutInput.showPortrait = choosePlusPresentation && !currentPortraitFileName.empty();
	layoutInput.message = currentMessage;
	layoutInput.speakerName = currentSpeakerName;
	for (size_t optionIndex = 0; optionIndex < currentOptions.size(); optionIndex++)
	{
		if (isChoiceOptionVisible(optionIndex))
		{
			layoutInput.visibleItems.push_back({ static_cast<int>(optionIndex), currentOptions[optionIndex] });
		}
	}

	ChooseMenuLayoutOutput layout = calculateChooseMenuLayout(layoutInput);
	currentPageIndex = layout.pageIndex;
	currentPageCount = layout.pageCount;
	rect = toRect(layout.panel);
	stretch = true;

	if (messageLabel != nullptr)
	{
		messageLabel->rect = toRect(layout.message);
		messageLabel->visible = !currentMessage.empty() && !layout.message.isEmpty();
		messageLabel->activated = messageLabel->visible;
		messageLabel->setStr(currentMessage);
	}

	if (speakerLabel != nullptr)
	{
		speakerLabel->rect = toRect(layout.speaker);
		speakerLabel->visible = layoutInput.showSpeaker && !layout.speaker.isEmpty();
		speakerLabel->activated = speakerLabel->visible;
		if (speakerLabel->visible && layout.speakerRight)
		{
			int estimatedTextWidth = static_cast<int>(TextLayout::countUtf8Characters(currentSpeakerName))
				* std::max(1, speakerLabel->fontSize);
			estimatedTextWidth = std::max(1, std::min(estimatedTextWidth, speakerLabel->rect.w));
			speakerLabel->rect.x += speakerLabel->rect.w - estimatedTextWidth;
			speakerLabel->rect.w = estimatedTextWidth;
		}
		speakerLabel->setStr(currentSpeakerName);
	}

	if (portraitImage != nullptr)
	{
		portraitImage->rect = toRect(layout.portrait);
		portraitImage->visible = layoutInput.showPortrait && !layout.portrait.isEmpty();
		portraitImage->activated = portraitImage->visible;
		portraitImage->impImage = portraitImage->visible
			? IMP::createIMPImage(std::string(HEAD_FOLDER_ASF) + currentPortraitFileName)
			: nullptr;
	}

	for (size_t displayIndex = 0; displayIndex < layout.pageItems.size(); displayIndex++)
	{
		const auto& positionedItem = layout.pageItems[displayIndex];
		auto button = getOrCreateChoiceButton(displayIndex);
		if (button == nullptr)
		{
			continue;
		}
		button->rect = toRect(positionedItem.rect);
		button->index = positionedItem.originalIndex;
		button->setStr(positionedItem.text);
		button->visible = true;
		button->activated = true;
		button->setSelected(multipleSelectionMode && isMultipleOptionSelected(button->index));
		choiceButtons.push_back(button);
	}

	if (currentPageCount > 1)
	{
		auto previousButton = getOrCreatePageButton(previousPageButton);
		previousButton->rect = toRect(layout.previousPage);
		previousButton->setStr("上一页 " + std::to_string(currentPageIndex + 1) + "/" + std::to_string(currentPageCount));
		previousButton->visible = !layout.previousPage.isEmpty();
		previousButton->activated = currentPageIndex > 0;

		auto nextButton = getOrCreatePageButton(nextPageButton);
		nextButton->rect = toRect(layout.nextPage);
		nextButton->setStr("下一页 " + std::to_string(currentPageIndex + 1) + "/" + std::to_string(currentPageCount));
		nextButton->visible = !layout.nextPage.isEmpty();
		nextButton->activated = currentPageIndex + 1 < currentPageCount;
	}

	if (multipleSelectionMode)
	{
		auto clearButton = getOrCreateMultipleControlButton(multipleClearButton, "清空");
		clearButton->rect = toRect(layout.clear);
		clearButton->setStr("清空");
		clearButton->visible = !layout.clear.isEmpty();
		clearButton->activated = !multipleSelection.selections().empty();

		auto confirmButton = getOrCreateMultipleControlButton(multipleConfirmButton, "确定");
		confirmButton->rect = toRect(layout.confirm);
		confirmButton->setStr("确定");
		confirmButton->visible = !layout.confirm.isEmpty();
		confirmButton->activated = multipleSelection.canConfirm();
		confirmButton->setSelected(multipleSelection.canConfirm());
	}

	configureFocus(previousFocusId);
}

void ChooseMenu::onWindowResize(int width, int height)
{
	(void)width;
	(void)height;
	std::string savedMessage = currentMessage;
	std::string savedOptionA = currentOptionA;
	std::string savedOptionB = currentOptionB;
	std::vector<std::string> savedOptions = currentOptions;
	std::vector<bool> savedVisibleOptions = currentVisibleOptions;
	std::string savedSpeakerName = currentSpeakerName;
	std::string savedPortraitFileName = currentPortraitFileName;
	int savedDialogPosition = currentDialogPosition;
	bool savedChoosePlusPresentation = choosePlusPresentation;
	bool savedDialogAlignedPresentation = dialogAlignedPresentation;
	bool savedIsInSelecting = isInSelecting;
	bool savedMultipleSelectionMode = multipleSelectionMode;
	bool savedKeyboardNavigationIndicatorVisible =
		keyboardNavigationIndicatorVisible;
	std::string savedFocusId = focusManager.getFocusedNodeId();
	int savedSelection = selection;
	int savedMultipleColumnCount = multipleColumnCount;
	int savedPageIndex = currentPageIndex;
	ChooseMultipleSelection savedMultipleSelection = multipleSelection;
	bool savedVisible = visible;
	bool savedLogicRunning = logicRunning;

	init();

	currentMessage = savedMessage;
	currentOptionA = savedOptionA;
	currentOptionB = savedOptionB;
	currentOptions = savedOptions;
	currentVisibleOptions = savedVisibleOptions;
	currentSpeakerName = savedSpeakerName;
	currentPortraitFileName = savedPortraitFileName;
	currentDialogPosition = savedDialogPosition;
	choosePlusPresentation = savedChoosePlusPresentation;
	dialogAlignedPresentation = savedDialogAlignedPresentation;
	isInSelecting = savedIsInSelecting;
	multipleSelectionMode = savedMultipleSelectionMode;
	keyboardNavigationIndicatorVisible =
		savedKeyboardNavigationIndicatorVisible;
	selection = savedSelection;
	multipleColumnCount = savedMultipleColumnCount;
	currentPageIndex = savedPageIndex;
	multipleSelection = savedMultipleSelection;
	visible = savedVisible;
	logicRunning = savedLogicRunning;
	applyChoiceState();
	if (!savedFocusId.empty())
	{
		focusManager.focusNode(savedFocusId);
	}
	updateKeyboardNavigationIndicator(
		savedKeyboardNavigationIndicatorVisible);
}
