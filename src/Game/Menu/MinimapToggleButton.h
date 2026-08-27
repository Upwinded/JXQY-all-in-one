#pragma once
#include "../../Component/RoundButton.h"

class MinimapToggleButton :
	public RoundButton
{
public:
	MinimapToggleButton();
	virtual ~MinimapToggleButton();

	bool checked = false;

	void setChecked(bool value);

private:
	virtual void onDraw() override;
	virtual void onClick() override;
};
