#pragma once
#include "../../Component/Panel.h"
class MsgBox :
	public Panel
{
public:
	MsgBox();
	virtual ~MsgBox();

	std::shared_ptr<Label> label = nullptr;
	bool showed = false;
	UTime beginTime = 0;
	UTime showinUTime = 3500;
	void showMessage(const std::string & str);
	virtual void onUpdate();
	virtual void init() override;
	void freeResource();
};

