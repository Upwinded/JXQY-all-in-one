#include "../Game/Menu/GambleMenuPolicy.h"
#include "../Game/Menu/GambleMenu.h"

#include <iostream>
#include <type_traits>

static_assert(std::is_same_v<
	decltype(&GambleMenu::onChildCallBack),
	void (GambleMenu::*)(PElement)>,
	"GambleMenu must override the callback consumed by fallback TextButton controls");

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}
}

int main()
{
	using namespace GambleMenuPolicy;
	bool ok = true;

	const Rectangle messageBox = { 180, 340, 280, 40 };
	const Rectangle localMessageLabel = { 30, 10, 220, 20 };
	const Rectangle resolvedMessageLabel = composeNestedRectangle(messageBox, localMessageLabel);
	ok = check(
		resolvedMessageLabel.x == 210 && resolvedMessageLabel.y == 350
			&& resolvedMessageLabel.width == 220 && resolvedMessageLabel.height == 20,
		"message label coordinates are composed relative to the message box") && ok;
	ok = check(contains(messageBox, resolvedMessageLabel),
		"resolved message label stays inside the message box") && ok;

	ok = check(actionForClick(Control::Primary, false, false) == Action::Roll,
		"generic non-gambling primary button starts the dice activity") && ok;
	ok = check(actionForClick(Control::Primary, false, true) == Action::Close,
		"generic non-gambling primary button closes after dice settlement") && ok;
	ok = check(actionForClick(Control::Exit, false, false) == Action::SettleAndClose,
		"generic non-gambling exit button settles and closes") && ok;
	ok = check(actionForClick(Control::IncreaseBet, false, false) == Action::None,
		"generic non-gambling mode ignores gambling-only controls") && ok;
	ok = check(actionForClick(Control::IncreaseBet, true, false) == Action::IncreaseBet,
		"gambling mode keeps bet controls") && ok;
	ok = check(actionForClick(Control::Primary, true, false) == Action::Roll,
		"gambling primary button resolves a round") && ok;
	ok = check(actionForModalEvent(ModalEvent::Escape) == Action::SettleAndClose,
		"Escape settles and closes the nested modal") && ok;
	ok = check(actionForModalEvent(ModalEvent::Quit) == Action::ExitApplication,
		"window close exits the application instead of being swallowed by the nested modal") && ok;

	return ok ? 0 : 1;
}
