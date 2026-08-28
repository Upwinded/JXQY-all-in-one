#include "../Game/Menu/ChooseMultipleSelection.h"

#include <iostream>
#include <vector>

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

bool checkSelection(const std::vector<int>& actual, const std::vector<int>& expected, const char* message)
{
	if (actual != expected)
	{
		std::cerr << "FAILED: " << message << " actual=";
		for (int value : actual)
		{
			std::cerr << value << ',';
		}
		std::cerr << " expected=";
		for (int value : expected)
		{
			std::cerr << value << ',';
		}
		std::cerr << '\n';
		return false;
	}
	return true;
}
}

int main()
{
	bool ok = true;
	ChooseMultipleSelection selection;

	selection.reset(2);
	ok = check(selection.limit() == 2, "reset stores positive limit") && ok;
	ok = check(!selection.canSatisfyWithAvailableOptions(1),
		"a filtered option set smaller than the limit is rejected instead of soft-locking") && ok;
	ok = check(selection.canSatisfyWithAvailableOptions(2),
		"an option set equal to the limit can start") && ok;
	ok = check(!selection.canConfirm(), "empty selection cannot confirm") && ok;
	ok = check(!selection.isFinished(), "empty selection is not finished") && ok;
	ok = checkSelection(selection.selections(), {}, "empty selection starts clear") && ok;

	selection.toggle(0);
	ok = checkSelection(selection.selections(), { 0 }, "first toggle selects option") && ok;
	ok = check(!selection.canConfirm(), "one selected option does not meet limit") && ok;
	ok = check(!selection.confirm(), "confirm fails before limit is reached") && ok;
	ok = check(!selection.isFinished(), "failed confirm does not finish") && ok;

	selection.toggle(1);
	ok = checkSelection(selection.selections(), { 0, 1 }, "second toggle selects option") && ok;
	ok = check(selection.canConfirm(), "selection can confirm at limit") && ok;
	ok = check(!selection.isFinished(), "reaching limit does not auto-finish") && ok;

	selection.toggle(2);
	ok = checkSelection(selection.selections(), { 0, 1 }, "toggle above limit is ignored") && ok;

	ok = check(selection.confirm(), "confirm succeeds at limit") && ok;
	ok = check(selection.isConfirmed(), "successful confirm records confirmed state") && ok;
	ok = check(selection.isFinished(), "successful confirm finishes selection") && ok;
	ok = checkSelection(selection.selections(), { 0, 1 }, "confirm preserves selected options") && ok;

	selection.toggle(0);
	ok = checkSelection(selection.selections(), { 0, 1 }, "finished selection ignores further toggles") && ok;

	selection.reset(3);
	selection.toggle(1);
	selection.toggle(2);
	ok = checkSelection(selection.selections(), { 1, 2 }, "reset allows new choices") && ok;
	selection.clear();
	ok = checkSelection(selection.selections(), {}, "clear removes selected options") && ok;
	ok = check(!selection.isFinished(), "clear does not finish selection") && ok;
	ok = check(!selection.canConfirm(), "clear disables confirm") && ok;

	selection.reset(-5);
	ok = check(selection.limit() == 0, "negative limit clamps to zero") && ok;
	selection.toggle(0);
	ok = checkSelection(selection.selections(), {}, "zero limit cannot select options") && ok;

	return ok ? 0 : 1;
}
