#pragma once
#include "BaseComponent.h"

class Button :
	public BaseComponent
{
public:
	Button();
	virtual ~Button();

	bool stretch = false;
	bool flat = false;
	bool animateFrames = false;
	bool hoverSoundEnabled = true;

	int buttonType = 0;

	std::string kind = "";

	_shared_imp image[3] = { nullptr, nullptr, nullptr };

	std::string sound[3] = { "", "", "" };

	void loadSound(const std::string & fileName, int index);
	
	void setRectFromImage();

	void freeImage();
	void freeSound();
	void freeResource();

	virtual void initFromIni(INIReader & ini);

protected:
	void playSound(int index);
	void draw();
	void draw(int x, int y);
	void drawFocusBorder();
	_shared_image loadButtonImage(int& xOffset, int& yOffset, int first, int second, int third);

	virtual void onClick();
	virtual void onExit();

	virtual void onMouseMoveIn(int x, int y);
	virtual void onMouseMoveOut();
	virtual void onMouseLeftDown(int x, int y);
	virtual void onMouseLeftUp(int x, int y);

public:
	virtual void onDraw();
};
