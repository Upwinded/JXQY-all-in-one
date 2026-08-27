#pragma once
#include "../../Component/Component.h"

class MsgBox :
	public ConfigDrivenPanel
{
public:
	MsgBox();
	virtual ~MsgBox();

	std::shared_ptr<Label> label = nullptr;
	bool showed = false;
	UTime beginTime = 0;
	UTime showinUTime = 3500;
	std::string currentMessage;
	void showMessage(const std::string & str, UTime duration = 3500);
	virtual void onUpdate() override;
	virtual void init() override;
	void freeResource();
	virtual void onWindowResize(int width, int height) override;
};
