#pragma once
#include "GameElement.h"

class Camera :
	public GameElement
{
public:
	Camera();
	virtual ~Camera();
private:
	double cameraSpeed = 40;

	const double flyRatio = 0.625 / 10.0;
	PointEx distanceToFly = { 0.0, 0.0 };
	PointEx distanceFlied = { 0.0, 0.0 };
	int flyDirection = 0;
	Point flyStartPosition = { 0, 0 };
	PointEx flyStartOffset = { 0.0, 0.0 };

public:
	GameElement * followNPC = nullptr;
	bool followPlayer = true;
	void flyTo(int dir, int distance);
	void flyToEx(int dir, int distance);

	void setFlyTo(int dir, int distance);
	void setFollowPlayer();

	void reArrangeNPC();
    PointEx differencePosition = { 0.0, 0.0 };

private:
	bool flying = false;
	virtual void onUpdate();
	virtual void onEvent();
};

