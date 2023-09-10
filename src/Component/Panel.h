#pragma once
#include "ImageContainer.h"
#include "Button.h"
#include "Scrollbar.h"
#include "Item.h"
#include "ListBox.h"
#include "Label.h"
#include "CheckBox.h"
#include "MemoText.h"
#include "ColumnImage.h"
#include "TalkLabel.h"
#include "TransImage.h"
#include "Joystick.h"
#include "RoundButton.h"
#include "TextButton.h"

#define addComponent(T,c,n) c=new T;\
                            c->initFromIni(n);\
                            addChild(c);

#define freeCom(component); \
	removeChild(component);\
	if (component != nullptr)\
	{\
		delete component;\
		component = nullptr;\
	}

class Panel :
	public ImageContainer
{
public:
	Panel();
	virtual ~Panel();

	Align align = alNone;
	int alignX = 0;
	int alignY = 0;
	void setAlign();
public:
	virtual void initFromIni(const std::string& fileName);
protected:

	Button * addButton(const std::string& fileName);
	Scrollbar * addScrollbar(const std::string& fileName);
	ImageContainer * addImageContainer(const std::string& fileName);
	Item * addItem(const std::string& fileName);
	ListBox * addListBox(const std::string & fileName);
	CheckBox * addCheckBox(const std::string & fileName);
	Label * addLabel(const std::string & fileName);
	TalkLabel * addTalkLabel(const std::string & fileName);
	MemoText * addMemo(const std::string & fileName);
	ColumnImage * addColumnImage(const std::string & fileName);
	TransImage * addTransImage(const std::string & fileName);
	Joystick* addJoystick(const std::string& fileName);
	RoundButton* addRoundButton(const std::string& fileName);
	TextButton* addTextButton(const std::string& fileName);



	void freeResource();


	void resetRect(Element * e, int x, int y);
	void resetRect();

};

