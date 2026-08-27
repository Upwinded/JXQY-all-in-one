#include "MemoMemu.h"
#include "../GameManager/GameManager.h"
#include "UIFocusManager.h"

MemoMenu::MemoMenu()
{
	name = "MemoMenu";
	visible = false;
	init();
}

MemoMenu::~MemoMenu()
{
	freeResource();
}

void MemoMenu::reFresh()
{
	if (scrollbar != nullptr && memoText != nullptr)
	{
		for (int i = 0; i < MEMO_LINE; i++)
		{
			if (position + i < (int)GameManager::getInstance()->memo.memo.size())
			{
				memoText->mstr[i]->setStr(GameManager::getInstance()->memo.memo[i + position]);
			}
			else
			{
				memoText->mstr[i]->setStr("");
			}
		}
	}
}

void MemoMenu::reset()
{
	if (scrollbar)
	{
		scrollbar->setPosition(0);
		position = scrollbar->position;
		reFresh();
	}
}

void MemoMenu::reRange(int max)
{
	if (scrollbar)
	{
		scrollbar->max = max;
		scrollbar->setPosition(scrollbar->position);
		position = scrollbar->position;
		reFresh();
		if (scrollbar->max <= scrollbar->min)
		{
			deactivateControllerFocus();
		}
	}
}

void MemoMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\memo\\memo.menu.ini");

	title = getComponentByName<ImageContainer>("title");
	image = getComponentByName<ImageContainer>("image");
	scrollbar = getComponentByName<Scrollbar>("scrollbar");
	memoText = getComponentByName<MemoText>("memoText");

	setChildRectReferToParent();
	configureControllerFocus();
}

void MemoMenu::freeResource()
{
	controllerFocusManager.clear();
	controllerFocusActive = false;
	memoText = nullptr;
	title = nullptr;
	image = nullptr;
	scrollbar = nullptr;
	ConfigDrivenPanel::freeResource();
}

void MemoMenu::configureControllerFocus()
{
	controllerFocusManager.clear();
	controllerFocusManager.setInputAwarePresentation();
	if (scrollbar == nullptr)
	{
		return;
	}
	controllerFocusManager.addNode(
		"memo-scrollbar",
		scrollbar,
		UIFocusManager::ActionHandler(),
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Up)
			{
				return scrollBy(-1);
			}
			if (direction == UIFocusDirection::Down)
			{
				return scrollBy(1);
			}
			return false;
		});
	controllerFocusManager.setDefaultFocus("memo-scrollbar");
	controllerFocusManager.setPagePreviousHandler(
		[this]() { scrollBy(-MEMO_LINE); });
	controllerFocusManager.setPageNextHandler(
		[this]() { scrollBy(MEMO_LINE); });
}

void MemoMenu::onEvent()
{
	if (scrollbar != nullptr && memoText != nullptr && position != scrollbar->position)
	{
		position = scrollbar->position;
		reFresh();
	}
}

bool MemoMenu::scrollBy(int delta)
{
	if (scrollbar == nullptr || delta == 0)
	{
		return false;
	}
	const int previousPosition = scrollbar->position;
	scrollbar->setPosition(previousPosition + delta);
	position = scrollbar->position;
	if (position != previousPosition)
	{
		reFresh();
		return true;
	}
	return false;
}

bool MemoMenu::activateControllerFocus(ControllerFocusTarget target)
{
	if (target != ControllerFocusTarget::Default
		|| controllerFocusCandidates().empty())
	{
		return false;
	}
	controllerFocusActive = true;
	controllerFocusManager.prepareForSemanticActivation();
	if (controllerFocusManager.restoreFocus())
	{
		return true;
	}
	controllerFocusActive = false;
	return false;
}

bool MemoMenu::isControllerFocusActive() const
{
	return controllerFocusActive
		&& !controllerFocusCandidates().empty()
		&& controllerFocusManager.getFocusedElement() != nullptr;
}

void MemoMenu::deactivateControllerFocus()
{
	controllerFocusActive = false;
	controllerFocusManager.suspendFocus();
}

PElement MemoMenu::controllerFocusedElement() const
{
	return controllerFocusActive
		? controllerFocusManager.getFocusedElement()
		: nullptr;
}

std::vector<PElement> MemoMenu::controllerFocusCandidates() const
{
	if (scrollbar == nullptr || scrollbar->max <= scrollbar->min)
	{
		return {};
	}
	return controllerFocusManager.getAvailableFocusElements();
}

bool MemoMenu::focusControllerElement(const PElement& element)
{
	if (controllerFocusCandidates().empty())
	{
		return false;
	}
	controllerFocusActive = true;
	controllerFocusManager.prepareForSemanticActivation();
	if (controllerFocusManager.focusElement(element))
	{
		return true;
	}
	controllerFocusActive = false;
	return false;
}

bool MemoMenu::onHandleUIAction(UIAction action)
{
	if (controllerFocusActive)
	{
		if (controllerFocusManager.handleAction(action))
		{
			return true;
		}
	}
	switch (action)
	{
	case UIAction::NavigateUp:
	case UIAction::ScrollUp:
		return scrollBy(-1);
	case UIAction::NavigateDown:
	case UIAction::ScrollDown:
		return scrollBy(1);
	case UIAction::PagePrevious:
		return scrollBy(-MEMO_LINE);
	case UIAction::PageNext:
		return scrollBy(MEMO_LINE);
	default:
		return false;
	}
}
