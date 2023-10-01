#pragma once
#include "BaseComponent.h"

#define SOUND_DYNAMIC_LOAD

class Button :
	public BaseComponent
{
public:
	Button();
	virtual ~Button();

	bool stretch = false;

	int buttonType = 0;

	std::string kind = "";

	_shared_imp image[3] = { nullptr, nullptr, nullptr };

#ifdef SOUND_DYNAMIC_LOAD
	std::string sound[3] = { "", "", "" };
#else
	_music sound[3] = { nullptr,nullptr,nullptr };
#endif // SOUND_DYNAMIC_LOAD

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

	virtual void onClick();
	virtual void onExit();

	virtual void onMouseMoveIn(int x, int y);
	virtual void onMouseMoveOut();
	virtual void onMouseLeftDown(int x, int y);
	virtual void onMouseLeftUp(int x, int y);

public:
	virtual void onDraw();
};

