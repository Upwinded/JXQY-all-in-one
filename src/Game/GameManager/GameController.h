#pragma once
#include "../../Element/Element.h"
#include "../Menu/Menu.h"

class GameController :
	public Element
{
public:
	GameController();
	virtual ~GameController();

	void freeResource();
	void init();
	virtual void onChildCallBack(Element* child);
	virtual void onEvent();
	virtual void onUpdate();
	virtual bool onHandleEvent(AEvent* e);

	bool MouseAlreadyDown = false;

#ifdef _MOBILE

	JoystickPanel* joystickPanel = nullptr;
	SkillsPanel* skillPanel = nullptr;
	void setFastSelectBtn(int index, bool sVisible, std::string str = "");

#endif // _MOBILE
private:
	int _last_magic_index = -1;

};

