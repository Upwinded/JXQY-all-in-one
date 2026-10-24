#pragma once

#include "../../Component/FlatTextButton.h"
#include "../Data/AttributePoints.h"
#include "ControllerPromptPresenter.h"

class AttributePointMenu : public Element
{
	friend class GamepadWorldRuntimeTestAccess;
public:
	AttributePointMenu();
	bool open(const PElement& origin, const PElement& returnFocus);
	void close(bool restoreFocus = true);

private:
	void layout(int width, int height);
	void positionRows();
	void select(int index);
	void change(std::size_t index, int delta);
	void submit();
	int pendingTotal() const;
	void drawText(CachedTextTexture& cache, const std::string& text, int x, int y, int width, int font, unsigned int color);
	bool onHandleEvent(AEvent& event) override;
	bool onHandleUIAction(UIAction action) override;
	void onChildCallBack(PElement child) override;
	void onWindowResize(int width, int height) override;
	void onUpdate() override;
	void onDraw() override;
	void onDrawEnd() override;

	Rect panel{};
	int openedCharacterIndex = -1;
	int selected = 0, firstRow = 0, rowsPerPage = 1;
	AttributePointAmounts pending{};
	std::string notice;
	std::weak_ptr<Element> origin, returnFocus;
	std::array<std::shared_ptr<FlatTextButton>, PlayerAttributeCount> plus, minus;
	std::array<CachedTextTexture, PlayerAttributeCount> names, values;
	std::shared_ptr<FlatTextButton> submitButton, resetButton, closeButton, previousButton, nextButton;
	CachedTextTexture titleTexture, balanceTexture, pageTexture, noticeTexture;
	ControllerPromptTextureCache promptTextures;
};
