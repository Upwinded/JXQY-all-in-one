#pragma once
#include "../../Component/Component.h"
#include "ControllerFocusParticipant.h"

class MapThumbnailMenuTestAccess;
class GamepadWorldRuntimeTestAccess;

class MapThumbnailMenu :
	public ConfigDrivenPanel,
	public ControllerFocusParticipant
{
public:
	MapThumbnailMenu();
	virtual ~MapThumbnailMenu();

	virtual void init() override;
	void updateThumbnail();
	void forceRegenerate();
	void setControllerVisible(bool newVisible);
	void toggleControllerVisible();
	virtual bool activateControllerFocus(
		ControllerFocusTarget target) override;
	virtual bool isControllerFocusActive() const override;
	virtual void deactivateControllerFocus() override;
	virtual std::shared_ptr<Element> controllerFocusedElement() const override;
	virtual std::vector<std::shared_ptr<Element>>
		controllerFocusCandidates() const override;
	virtual bool focusControllerElement(
		const std::shared_ptr<Element>& element) override;

private:
	friend class MapThumbnailMenuTestAccess;
	friend class GamepadWorldRuntimeTestAccess;
	virtual bool onHandleEvent(AEvent& e) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onEvent() override;
	virtual void onUpdate() override;
	virtual void onDrawEnd() override;
	virtual void onWindowResize(int width, int height) override;
	void freeResource();
	bool canQueueWorldMovement() const;
	bool queueMovementAtThumbnailPixel(int x, int y, bool running);
	bool queueMovementToTile(Point target, bool running);
	void initializeControllerCursor();
	void clearControllerCursor();
	bool moveControllerCursor(float deltaX, float deltaY);
	void setControllerCursorFromPixel(int pixelX, int pixelY);
	Point getControllerCursorPixel() const;
	void drawControllerCursor();
	void drawEntityMarkers();
	void drawMarker(int pixelX, int pixelY, int radius, float r, float g, float b, float a);
	float getMarkerEdgeAlpha(int pixelX, int pixelY) const;
	Point tileToThumbnailPixel(Point tile, PointEx offset);
	bool thumbnailPixelToTile(int x, int y, Point& tile);
	void adjustThumbnailContainerRect();
	Rect getScaledPanelRect(int x, int y, int width, int height);
	Rect expandAndClampRect(Rect target, int marginX, int marginY, Rect bounds);

	std::shared_ptr<ImageContainer> thumbnailContainer = nullptr;
	std::shared_ptr<Label> mapNameLabel = nullptr;
	std::shared_ptr<Button> closeButton = nullptr;
	std::string currentMapName;
	float controllerCursorNormalizedX = 0.5f;
	float controllerCursorNormalizedY = 0.5f;
	bool controllerCursorActive = false;
	bool controllerFocusActive = false;
};
