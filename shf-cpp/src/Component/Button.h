#pragma once
#include "..\Element\Element.h"

#define SOUND_DYNAMIC_LOAD

class Button :
	public Element
{
public:
	Button();
	~Button();

	bool stretch = false;

	int buttonType = 0;

	std::string kind = "";

	IMPImage * image[3] = { NULL, NULL, NULL };

#ifdef SOUND_DYNAMIC_LOAD
	std::string sound[3] = { "", "", "" };
#else
	_music sound[3] = { NULL,NULL,NULL };
#endif // SOUND_DYNAMIC_LOAD

	void loadSound(const std::string & fileName, int index);
	
	void setRectFromImage();

	void freeImage();
	void freeSound();
	virtual void freeResource();

	virtual void initFromIni(const std::string & fileName);

protected:
	void playSound(int index);
private:
	virtual void onClick();
	virtual void onDraw();
	virtual void onExit();

	virtual void onMouseMoveIn();
	virtual void onMouseMoveOut();
	virtual void onMouseLeftDown();
	virtual void onMouseLeftUp();


};

