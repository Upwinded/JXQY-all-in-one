#include "MinimapToggleButton.h"
#include "../../Engine/Engine.h"
#include <algorithm>

MinimapToggleButton::MinimapToggleButton()
{
	name = "MinimapToggleButton";
	canCallBack = true;
	initFromIniFileName("ini\\ui\\mobile\\skills\\minimap.ini");
	name = "MinimapToggleButton";
	canCallBack = true;
}

MinimapToggleButton::~MinimapToggleButton()
{
	freeResource();
}

void MinimapToggleButton::setChecked(bool value)
{
	checked = value;
}

void MinimapToggleButton::onDraw()
{
	RoundButton::onDraw();
	if (!checked)
	{
		return;
	}

	int lineWidth = std::max(2, rect.w / 18);
	int pad = rect.w / 4;
	engine->fillRect(rect.x + pad, rect.y + rect.h - pad, rect.w - pad * 2, lineWidth, 255, 220, 92, 230);
}

void MinimapToggleButton::onClick()
{
	checked = !checked;
	RoundButton::onClick();
}
