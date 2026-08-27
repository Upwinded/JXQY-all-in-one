#pragma once
#include "../../Component/ConfigDrivenPanel.h"

class StateMenu :
	public ConfigDrivenPanel
{
public:
	StateMenu();
	virtual ~StateMenu();

	virtual void updateLabel();
	void updatePanelImage();

	virtual void init() override;

private:
	std::shared_ptr<ImageContainer> image = nullptr;
	int loadedPanelIndex = -1;
	virtual void onUpdate() override;
	void freeResource();
};
