#pragma once
#include "GameElement.h"

class Camera :
	public GameElement
{
public:
	Camera();
	~Camera();
	unsigned int flyTime = 0;
	unsigned int endFlyTime = 0;
	Point dest = { 0, 0 };
	double cameraSpeed = 0.3;
	bool followPlayer = true;


	void flyTo(Point d);

	void setFlyTo(Point d);
	void setFollowPlayer();

	void reArrangeNPC();

private:
	bool flying = false;
	virtual void onUpdate();
	virtual void onEvent();
};

