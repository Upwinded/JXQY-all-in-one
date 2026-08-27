#pragma once

namespace GambleMenuPolicy
{
struct Rectangle
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

constexpr Rectangle composeNestedRectangle(const Rectangle& parent, const Rectangle& child)
{
	return {
		parent.x + child.x,
		parent.y + child.y,
		child.width,
		child.height,
	};
}

constexpr bool contains(const Rectangle& parent, const Rectangle& child)
{
	return child.x >= parent.x
		&& child.y >= parent.y
		&& child.x + child.width <= parent.x + parent.width
		&& child.y + child.height <= parent.y + parent.height;
}

enum class Control
{
	None,
	DecreaseBet,
	IncreaseBet,
	Small,
	Big,
	Primary,
	Exit,
};

enum class Action
{
	None,
	DecreaseBet,
	IncreaseBet,
	SelectSmall,
	SelectBig,
	Roll,
	Close,
	SettleAndClose,
	ExitApplication,
};

enum class ModalEvent
{
	None,
	Escape,
	Quit,
};

constexpr Action actionForClick(Control control, bool gambleMode, bool settled)
{
	switch (control)
	{
	case Control::DecreaseBet:
		return gambleMode && !settled ? Action::DecreaseBet : Action::None;
	case Control::IncreaseBet:
		return gambleMode && !settled ? Action::IncreaseBet : Action::None;
	case Control::Small:
		return gambleMode && !settled ? Action::SelectSmall : Action::None;
	case Control::Big:
		return gambleMode && !settled ? Action::SelectBig : Action::None;
	case Control::Primary:
		return settled ? Action::Close : Action::Roll;
	case Control::Exit:
		return Action::SettleAndClose;
	default:
		return Action::None;
	}
}

constexpr Action actionForModalEvent(ModalEvent event)
{
	switch (event)
	{
	case ModalEvent::Escape:
		return Action::SettleAndClose;
	case ModalEvent::Quit:
		return Action::ExitApplication;
	default:
		return Action::None;
	}
}
}
