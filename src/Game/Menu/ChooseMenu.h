#pragma once
#include "../../Component/Component.h"
#include "../../Component/ChooseTextButton.h"
#include "ChooseMenuLayout.h"
#include "ChooseMultipleSelection.h"
#include "UIFocusManager.h"
#include <vector>

class GamepadEssentialUITestAccess;
class GamepadSurfaceContractTestAccess;

class ChooseMenu :
	public ConfigDrivenPanel
{
	friend class GamepadEssentialUITestAccess;
	friend class GamepadSurfaceContractTestAccess;
public:
	ChooseMenu();
	virtual ~ChooseMenu();

	void choose(const std::string& message, const std::string& optionA, const std::string& optionB);
	void chooseEx(const std::string& message, const std::vector<std::string>& options, const std::vector<bool>& visibleOptions);
	void choosePlus(const std::string& speakerName, const std::string& portraitFileName, int dialogPosition, const std::string& message, const std::vector<std::string>& options, const std::vector<bool>& visibleOptions);
	void chooseMultiple(const std::string& message, const std::vector<std::string>& options, const std::vector<bool>& visibleOptions, int columnCount, int selectionCount);
	int getSelection();
	std::vector<int> getMultipleSelection() const;
	bool hasMultiplePages() const { return currentPageCount > 1; }

	virtual void init() override;

private:
	struct SelectionConfiguration
	{
		std::string message;
		std::vector<std::string> options;
		std::vector<bool> visibleOptions;
		bool multiple = false;
		int columnCount = 1;
		int selectionCount = 0;
		bool choosePlus = false;
		bool alignToDialog = false;
		std::string speakerName;
		std::string portraitFileName;
		int dialogPosition = 2;
	};
	
	bool isInSelecting = false;
	bool multipleSelectionMode = false;
	bool choosePlusPresentation = false;
	bool dialogAlignedPresentation = false;
	int selection = -1;
	int multipleColumnCount = 1;
	int currentPageIndex = 0;
	int currentPageCount = 1;
	bool keyboardNavigationIndicatorVisible = false;
	ChooseMultipleSelection multipleSelection;

	std::shared_ptr<Label> messageLabel = nullptr;
	std::shared_ptr<Label> speakerLabel = nullptr;
	std::shared_ptr<ImageContainer> portraitImage = nullptr;
	std::shared_ptr<ChooseTextButton> selectA = nullptr;
	std::shared_ptr<ChooseTextButton> selectB = nullptr;
	std::shared_ptr<ChooseTextButton> previousPageButton = nullptr;
	std::shared_ptr<ChooseTextButton> nextPageButton = nullptr;
	std::shared_ptr<ChooseTextButton> multipleConfirmButton = nullptr;
	std::shared_ptr<ChooseTextButton> multipleClearButton = nullptr;
	std::vector<std::shared_ptr<ChooseTextButton>> choiceButtons;
	std::string currentMessage;
	std::string currentOptionA;
	std::string currentOptionB;
	std::vector<std::string> currentOptions;
	std::vector<bool> currentVisibleOptions;
	std::string currentSpeakerName;
	std::string currentPortraitFileName;
	int currentDialogPosition = 2;
	Rect preferredPanelRect = { 420, 590, 440, 120 };
	Rect preferredMessageRect = { 65, 10, 310, 40 };
	Rect preferredOptionRect = { 65, 52, 310, 22 };
	int preferredOptionRowGap = 2;
	UIFocusManager focusManager;

	void freeResource();
	void configureFocus(const std::string& preferredFocusId = "");
	bool isChoiceOptionVisible(size_t optionIndex) const;
	int getVisibleOptionCount() const;
	bool isMultipleOptionSelected(int optionIndex) const;
	void toggleMultipleOption(int optionIndex);
	void finishMultipleSelection();
	void clearMultipleSelection();
	void resetPresentation();
	void updateKeyboardNavigationIndicator(bool visible);
	bool prepareSelection(const SelectionConfiguration& configuration);
	void chooseOptions(
		const std::string& message,
		const std::vector<std::string>& options,
		const std::vector<bool>& visibleOptions,
		bool alignToDialog);
	void selectChoice(int optionIndex);
	void changePage(int offset);
	void applyChoiceState();
	std::shared_ptr<ChooseTextButton> getOrCreateChoiceButton(size_t displayIndex);
	std::shared_ptr<ChooseTextButton> getOrCreateMultipleControlButton(std::shared_ptr<ChooseTextButton>& button, const std::string& text);
	std::shared_ptr<ChooseTextButton> getOrCreatePageButton(std::shared_ptr<ChooseTextButton>& button);
	void updateChoiceButtons();
	virtual void onEvent() override;
	virtual bool onHandleEvent(AEvent& event) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onWindowResize(int width, int height) override;
};
