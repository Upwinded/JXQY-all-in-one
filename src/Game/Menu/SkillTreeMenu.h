#pragma once

#include "../../Component/FlatTextButton.h"
#include "../../Component/TextLayout.h"
#include "../Data/Magic.h"
#include "ControllerPromptPresenter.h"

class SkillTreeMenu : public Element
{
	friend class GamepadWorldRuntimeTestAccess;
public:
	SkillTreeMenu();
	bool open();
	void close(bool restoreFocus = true);

private:
	void layout(int width, int height);
	void positionNodes(bool revealSelection);
	void selectNode(std::size_t index);
	void navigate(int dx, int dy);
	void changeGroup(int direction);
	void scroll(int direction);
	void turnDetails(int direction);
	void confirmLearning();
	void refreshContent();
	void drawText(CachedTextTexture& cache, const std::string& text,
		int x, int y, int width, int font, unsigned int color);
	bool onHandleEvent(AEvent& event) override;
	bool onHandleUIAction(UIAction action) override;
	void onChildCallBack(PElement child) override;
	void onWindowResize(int width, int height) override;
	void onUpdate() override;
	void onDraw() override;
	void onDrawEnd() override;

	Rect panel = {}, chart = {}, details = {};
	std::size_t selected = 0;
	std::size_t group = 0;
	int openedCharacterIndex = -1;
	int scrollOffset = 0;
	int scrollMaximum = 0;
	int detailPage = 0;
	int detailLinesPerPage = 1;
	bool confirming = false;
	std::string notice;
	std::string detailContent;
	std::vector<std::string> nodeIds, names, descriptions;
	std::vector<std::shared_ptr<FlatTextButton>> nodeButtons;
	std::shared_ptr<FlatTextButton> learnButton, closeButton, upButton, downButton;
	std::shared_ptr<FlatTextButton> previousGroup, nextGroup, previousDetails, nextDetails;
	std::vector<TextLayout::ColorTaggedTextLine> detailLines;
	std::vector<CachedTextTexture> detailTextures;
	CachedTextTexture titleTexture, pointsTexture, selectedTexture, pageTexture;
	ControllerPromptTextureCache promptTextures;
};
