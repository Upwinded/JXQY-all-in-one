#pragma once

#include "../../Component/FlatTextButton.h"
#include "../../Component/TextLayout.h"
#include "ControllerPromptPresenter.h"

class PaperMenu : public Element
{
	friend class GamepadWorldRuntimeTestAccess;
public:
	PaperMenu();
	void open(int type, const std::vector<std::string>& contents);
	void close();

private:
	struct Page
	{
		std::size_t sourceIndex = 0;
		std::vector<TextLayout::ColorTaggedTextLine> lines;
	};
	void layout(int width, int height);
	void selectPage(std::size_t index);
	void advance();
	bool onHandleEvent(AEvent& event) override;
	bool onHandleUIAction(UIAction action) override;
	void onChildCallBack(PElement child) override;
	void onWindowResize(int width, int height) override;
	void onDraw() override;
	void onDrawEnd() override;

	int paperType = 0;
	int fontSize = 20;
	int lineHeight = 28;
	Rect paperRect = {};
	Rect contentRect = {};
	std::vector<std::string> sourcePages;
	std::vector<Page> pages;
	std::size_t pageIndex = 0;
	std::shared_ptr<FlatTextButton> previousButton;
	std::shared_ptr<FlatTextButton> nextButton;
	std::shared_ptr<FlatTextButton> closeButton;
	std::vector<CachedTextTexture> textTextures;
	CachedTextTexture titleTexture;
	CachedTextTexture counterTexture;
	_shared_image pageImage = nullptr;
	ControllerPromptTextureCache promptTextures;
};
