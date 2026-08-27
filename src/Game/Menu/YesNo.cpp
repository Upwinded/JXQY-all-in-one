#include "YesNo.h"
#include "../../Engine/Engine.h"
#include "ControllerPromptPresenter.h"

YesNo::YesNo(const std::string & s)
{
	name = "yesnoMenu";
	setPriority(epMax);
	focusManager.setInputAwarePresentation();
	init(s);
}


YesNo::~YesNo()
{
	freeResource();
}

void YesNo::init(const std::string& s)
{
	promptText = s;
	init();
}

void YesNo::init()
{
	const std::string preferredFocusId = focusManager.getFocusedNodeId();
	freeResource();
	loadMenuDefinition("ini\\ui\\yesno\\yesno.menu.ini");

	yes = getComponentByName<Button>("yes");
	no = getComponentByName<Button>("no");
	label = getComponentByName<Label>("label");
	if (yes == nullptr || no == nullptr || label == nullptr)
	{
		// Some original profiles provide the Yes/No window and components but
		// not the aggregate menu definition. Build the same panel from those
		// stable component files so global confirmations remain available.
		freeResource();
		initFromIniFileName("ini\\ui\\yesno\\window.ini");
		yes = std::dynamic_pointer_cast<Button>(
			createComponentByType("Button", "ini\\ui\\yesno\\btnyes.ini"));
		no = std::dynamic_pointer_cast<Button>(
			createComponentByType("Button", "ini\\ui\\yesno\\btnno.ini"));
		label = std::dynamic_pointer_cast<Label>(
			createComponentByType("Label", "ini\\ui\\yesno\\label.ini"));
	}
	if (label != nullptr)
	{
		label->setStr(promptText);
	}

	setChildRectReferToParent();
	configureFocus(preferredFocusId);
}

void YesNo::freeResource()
{
	focusManager.clear();
	yes = nullptr;
	no = nullptr;
	label = nullptr;
	ConfigDrivenPanel::freeResource();
}

void YesNo::configureFocus(const std::string& preferredFocusId)
{
	focusManager.clear();
	focusManager.addVisualSpatialGroup(
		"yes-no-actions",
		{
			{ "yes", yes, [this]() { selectYes(); } },
			{ "no", no, [this]() { selectNo(); } }
		});
	focusManager.applyConfigDrivenFocusNavigation(
		*this,
		{
			{ "yes", "yes" },
			{ "no", "no" },
	});
	focusManager.setDefaultFocus(no != nullptr ? "no" : "yes");
	focusManager.setCancelHandler([this]() { selectNo(); });
	if (preferredFocusId.empty()
		|| !focusManager.focusNode(preferredFocusId))
	{
		focusManager.focusDefault();
	}
}

void YesNo::selectYes()
{
	logicRunning = false;
	result = erOK;
}

void YesNo::selectNo()
{
	logicRunning = false;
	result = erExit;
}

void YesNo::onEvent()
{
	if (yes && yes->getResult(erClick))
	{
		selectYes();
	}
	else if (no && no->getResult(erClick))
	{
		selectNo();
	}
}

bool YesNo::onHandleEvent(AEvent & e)
{
	if ((e.eventType == ET_MOUSEDOWN
			&& e.eventData == MBC_MOUSE_LEFT)
		|| e.eventType == ET_FINGERDOWN)
	{
		adoptUIFocusPointerTarget(
			e.eventType == ET_MOUSEDOWN ? TOUCH_MOUSEID : e.eventData);
	}
	if (dispatchKeyboardUIAction(e, *this))
	{
		return true;
	}
	return false;
}

bool YesNo::onHandleUIAction(UIAction action)
{
	return focusManager.handleAction(action);
}

void YesNo::onDrawEnd()
{
	if (!visible || engine == nullptr
		|| !Element::isCurrentRunOwner(this)
		|| !focusManager.isFocusPresented())
	{
		return;
	}
	using GameInput::InputAction;
	const std::vector<ControllerPromptItem> items =
	{
		{ InputAction::NavigateUp, "选择" },
		{ InputAction::Confirm, "确认" },
		{ InputAction::Cancel, "否/返回" }
	};
	ControllerPromptPresenter::drawBottomBar(
		engine, engine->inputActions(), items);
}

void YesNo::onRun()
{
	focusManager.focusDefault();
}
