#pragma once
#include "RoundButton.h"
#include <vector>

#define JOYSTICK_MIN_RANGE 1 / 20
#define JOYSTICK_MID_RANGE 1 / 4
#define OutRange -1000

class Joystick :
	public RoundButton
{
public:
	Joystick():RoundButton() {}
	virtual ~Joystick() { freeResource(); }
protected:
	Point touchPosition = { OutRange, OutRange };
private:
	float calculateAngel(int x, int y);
	int calculateDirection(float angle);
	int normalizeDirection(int dir);
public:
	std::vector<int> getDirectionList();
	bool isRunning();
	bool isWalking();
	int distanceToCenter();
protected:
	virtual bool mouseInRect(int x, int y);
public:
	virtual void onMouseMoving(int x, int y);
	virtual void onMouseMoveIn(int x, int y);
	virtual void onMouseMoveOut();
	virtual void onMouseLeftUp(int x, int y);
	virtual void onMouseLeftDown(int x, int y);
	virtual void onClick() {}
	virtual void onDraw();


	void freeResource();
public:
	virtual void initFromIni(INIReader & ini);
};

