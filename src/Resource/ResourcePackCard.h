#pragma once

#include "../Component/Button.h"
#include "../Component/FlatTextButton.h"
#include "../Component/Label.h"

struct ResourcePackCardContent
{
	std::string title;
	std::string authorAndVersion;
	bool showDescriptionAction = false;
	bool wasRecentlySelected = false;
	bool onlineOnly = false;
};

class GamepadEssentialUITestAccess;

class ResourcePackCard : public Button
{
	friend class GamepadEssentialUITestAccess;
public:
	ResourcePackCard();
	virtual ~ResourcePackCard();

	void setContent(const ResourcePackCardContent& value);
	void setLayout(const Rect& value);
	void setSelected(bool value);
	bool isSelected() const;
	bool ownsPointerInteraction(EventTouchID pointerID) const;
	bool ownsBodyPointerInteraction(EventTouchID pointerID) const;
	bool isDescriptionActionPoint(int x, int y) const;
	bool takeDescriptionActionRequested();
	void setFrameImages(_shared_image normal, _shared_image selectedImage);
	virtual void onChildCallBack(PElement child) override;

protected:
	virtual void onDraw() override;
	virtual void onDrawEnd() override;

private:
	void updateDescriptionButtonLayout();
	void updateTextLayout(bool hovered, bool usingFrameImage);
	void drawHoverBorder(bool pressed);
	void drawSelectionBox();
	void drawSelectionBorder();
	void drawOnlineOnlyBadge();
	void drawRecentSelectionBadge();
	int getHorizontalPadding() const;
	bool getSelectionBoxRect(Rect& selectionBoxRect) const;
	bool getOnlineOnlyBadgeRect(Rect& badgeRect) const;
	bool getRecentSelectionBadgeRect(Rect& badgeRect) const;

	ResourcePackCardContent content;
	std::shared_ptr<Label> titleLabel;
	std::shared_ptr<Label> authorVersionLabel;
	std::shared_ptr<FlatTextButton> descriptionButton;
	_shared_image normalFrameImage = nullptr;
	_shared_image selectedFrameImage = nullptr;
	bool selected = false;
	bool descriptionActionRequested = false;
	bool onlineOnlyBadgeVisible = false;
	bool recentSelectionBadgeVisible = false;
};
