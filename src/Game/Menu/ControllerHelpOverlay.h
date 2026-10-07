#pragma once

#include "../../Element/Element.h"
#include "../../Component/Label.h"
#include "ControllerPromptPresenter.h"

#include <array>
#include <string>
#include <vector>

class ControllerHelpOverlay : public Element
{
	friend class GamepadEssentialUITestAccess;

public:
	ControllerHelpOverlay();

	void dismiss();

private:
	struct HelpLine
	{
		GameInput::InputAction action;
		std::string description;
	};
	static const std::vector<HelpLine>& worldHelpLines();
	static const std::vector<HelpLine>& menuHelpLines();
	void drawSection(
		const std::string& title,
		const std::vector<HelpLine>& lines,
		std::vector<CachedTextTexture>& textTextures,
		int x,
		int y,
		int fontSize,
		int lineHeight,
		unsigned int textColor);
	void drawControllerDiagram(
		int contentLeft,
		int contentTop,
		int outerMargin,
		int lineHeight);
	bool ensureControllerImageLoaded();
	std::string formatLine(const HelpLine& line) const;
	void updateLayout(int width, int height);

	bool onHandleEvent(AEvent& event) override;
	bool onHandleUIAction(UIAction action) override;
	void onDraw() override;
	void onWindowResize(int width, int height) override;

	ControllerPromptLabelTheme labelTheme;
	_shared_image controllerImage = nullptr;
	std::array<CachedTextTexture, 3> headerAndFooterTextTextures;
	std::vector<CachedTextTexture> worldHelpTextTextures;
	std::vector<CachedTextTexture> menuHelpTextTextures;
	bool controllerImageLoadAttempted = false;
	int windowWidth = 0;
	int windowHeight = 0;
};
