#include "TopMenu.h"
#include "../GameManager/GameManager.h"

TopMenu::TopMenu()
{
	name = "TopMenu";
	visible = true;
	init();
}

TopMenu::~TopMenu()
{
	removeAllChild();
	freeResource();
}

void TopMenu::init()
{
	std::string previousFocusId =
		controllerFocusManager.getFocusedNodeId();
	if (previousFocusId.empty())
	{
		previousFocusId = pendingControllerFocusNodeId;
	}
	pendingControllerFocusNodeId.clear();
	const bool focusWasActive = controllerFocusActive;
	freeResource();
	loadMenuDefinition("ini\\ui\\top\\top.menu.ini");

	equipBtn = getComponentByName<CheckBox>("equipBtn");
	goodsBtn = getComponentByName<CheckBox>("goodsBtn");
	magicBtn = getComponentByName<CheckBox>("magicBtn");
	notesBtn = getComponentByName<CheckBox>("notesBtn");
	optionBtn = getComponentByName<CheckBox>("optionBtn");
	stateBtn = getComponentByName<CheckBox>("stateBtn");
	xiulianBtn = getComponentByName<CheckBox>("xiulianBtn");

	setChildRectReferToParent();
	configureControllerFocus();
	if (!previousFocusId.empty())
	{
		if (focusWasActive)
		{
			controllerFocusActive = true;
			controllerFocusManager.prepareForSemanticActivation();
			if (!controllerFocusManager.focusNode(previousFocusId))
			{
				controllerFocusActive =
					controllerFocusManager.restoreFocus();
				if (!controllerFocusActive)
				{
					pendingControllerFocusNodeId = previousFocusId;
				}
			}
		}
		else if (controllerFocusManager.focusNode(previousFocusId))
		{
			controllerFocusManager.suspendFocus();
		}
		else
		{
			// Hidden ancestors intentionally make their nodes unavailable.
			// Preserve the stable node ID until ShowInterface makes this owner
			// eligible for semantic focus again.
			pendingControllerFocusNodeId = previousFocusId;
		}
	}
}

void TopMenu::configureControllerFocus()
{
	controllerFocusManager.clear();
	controllerFocusManager.setInputAwarePresentation();
	const std::vector<UIFocusNodeBinding> bindings =
	{
		{ "top-state", stateBtn,
			[this]() { gm->menu->toggleStateView(); } },
		{ "top-equip", equipBtn,
			[this]() { gm->menu->toggleEquipView(); } },
		{ "top-practice", xiulianBtn,
			[this]() { gm->menu->togglePracticeView(); } },
		{ "top-goods", goodsBtn,
			[this]() { gm->menu->toggleGoodsView(); } },
		{ "top-magic", magicBtn,
			[this]() { gm->menu->toggleMagicView(); } },
		{ "top-memo", notesBtn,
			[this]() { gm->menu->toggleMemoView(); } },
		{ "top-options", optionBtn,
			[this]() { gm->menu->openSystemMenu(); } },
	};
	const std::vector<std::string> ids =
		controllerFocusManager.addVisualSpatialGroup(
			"top-menu-buttons",
			bindings);
	controllerFocusManager.applyConfigDrivenFocusNavigation(
		*this,
		{
			{ "stateBtn", "top-state" },
			{ "equipBtn", "top-equip" },
			{ "xiulianBtn", "top-practice" },
			{ "goodsBtn", "top-goods" },
			{ "magicBtn", "top-magic" },
			{ "notesBtn", "top-memo" },
			{ "optionBtn", "top-options" },
		});
	if (!ids.empty())
	{
		controllerFocusManager.setDefaultFocus(ids.front());
	}
}

bool TopMenu::activateControllerFocus(ControllerFocusTarget target)
{
	if (target != ControllerFocusTarget::Default)
	{
		return false;
	}
	controllerFocusActive = true;
	controllerFocusManager.prepareForSemanticActivation();
	if (!pendingControllerFocusNodeId.empty())
	{
		const std::string pendingFocusId = pendingControllerFocusNodeId;
		pendingControllerFocusNodeId.clear();
		if (controllerFocusManager.focusNode(pendingFocusId))
		{
			return true;
		}
	}
	if (controllerFocusManager.restoreFocus())
	{
		return true;
	}
	controllerFocusActive = false;
	return false;
}

bool TopMenu::isControllerFocusActive() const
{
	return controllerFocusActive
		&& controllerFocusManager.getFocusedElement() != nullptr;
}

void TopMenu::deactivateControllerFocus()
{
	controllerFocusActive = false;
	controllerFocusManager.suspendFocus();
}

PElement TopMenu::controllerFocusedElement() const
{
	return controllerFocusActive
		? controllerFocusManager.getFocusedElement()
		: nullptr;
}

std::vector<PElement> TopMenu::controllerFocusCandidates() const
{
	return controllerFocusManager.getAvailableFocusElements();
}

bool TopMenu::focusControllerElement(const PElement& element)
{
	controllerFocusActive = true;
	controllerFocusManager.prepareForSemanticActivation();
	if (controllerFocusManager.focusElement(element))
	{
		pendingControllerFocusNodeId.clear();
		return true;
	}
	controllerFocusActive = false;
	return false;
}

void TopMenu::onEvent()
{
	if (optionBtn && optionBtn->getResult(erClick))
	{
		gm->menu->openSystemMenu();
	}
	if (equipBtn && equipBtn->getResult(erClick))
	{
		gm->menu->toggleEquipView();
	}
	if (stateBtn && stateBtn->getResult(erClick))
	{
		gm->menu->toggleStateView();
	}
	if (xiulianBtn && xiulianBtn->getResult(erClick))
	{
		gm->menu->togglePracticeView();
	}
	if (goodsBtn && goodsBtn->getResult(erClick))
	{
		gm->menu->toggleGoodsView();
	}
	if (magicBtn && magicBtn->getResult(erClick))
	{
		gm->menu->toggleMagicView();
	}
	if (notesBtn && notesBtn->getResult(erClick))
	{
		gm->menu->toggleMemoView();
	}
}

bool TopMenu::onHandleUIAction(UIAction action)
{
	return controllerFocusActive
		&& controllerFocusManager.handleAction(action);
}

void TopMenu::freeResource()
{
	controllerFocusManager.clear();
	controllerFocusActive = false;
	equipBtn = nullptr;
	goodsBtn = nullptr;
	magicBtn = nullptr;
	optionBtn = nullptr;
	notesBtn = nullptr;
	stateBtn = nullptr;
	xiulianBtn = nullptr;
	ConfigDrivenPanel::freeResource();
}
