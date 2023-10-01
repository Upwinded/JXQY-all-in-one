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
	virtual void onChildCallBack(PElement child);
	virtual void onEvent();
	virtual void onUpdate();
	virtual bool onHandleEvent(AEvent & e);

	bool MouseAlreadyDown = false;

#ifdef __MOBILE__

	std::shared_ptr<JoystickPanel> joystickPanel = nullptr;
	std::shared_ptr<SkillsPanel> skillPanel = nullptr;
	void setFastSelectBtn(int index, bool sVisible, std::string str = "");

#endif // __MOBILE__
private:
	int _last_magic_index = -1;

	Point getPlayerRelativePosition(double angle, double distance, double xFactor);

};

