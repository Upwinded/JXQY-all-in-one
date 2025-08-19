#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class SaveLoad :
	public Panel
{
public:
	SaveLoad(bool s, bool l);
	virtual ~SaveLoad();

private:
	bool save = true;
	bool load = true;

public:
	int index = -1;

	std::shared_ptr<ListBox> listBox = nullptr;

	std::shared_ptr<ImageContainer> snap = nullptr;
	std::shared_ptr<Button> loadBtn = nullptr;
	std::shared_ptr<Button> saveBtn = nullptr;
	std::shared_ptr<Button> exitBtn = nullptr;

	virtual void init() override;

private:
	virtual void onEvent();
	void freeResource();
	virtual bool onHandleEvent(AEvent & e);

};

