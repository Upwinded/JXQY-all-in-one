#pragma once
#include "BaseComponent.h"
#include "DragButton.h"

class Scrollbar :
	public BaseComponent
{
public:
	Scrollbar();
	virtual ~Scrollbar();

	ScrollbarStyle style = ssVertical;
	int position = 0;
	int min = 0;
	int max = 72;

	int lineSize = 3;
	int pageSize = 9;
	int slideBegin = 5;
	int slideEnd = 145;

	void positionChanged(int tempPosition);

	DragButton * slideBtn = nullptr;

	Rect slideRect = { 0, 0, 0, 0 };
	Rect lastRect = { 0, 0, 0, 0 };

	_shared_imp impImage = nullptr;

	void freeResource();

	virtual void initFromIni(const std::string & fileName);
	void limitPos(int * p, int minp, int maxp);

	void setSlideBtnRect();

	void setPosition(int pos);

private:
	virtual void onUpdate();
	virtual void onMouseLeftDown(int x, int y);
	virtual void onDraw();
	virtual void onExit();
	virtual void onSetChildRect();

};

