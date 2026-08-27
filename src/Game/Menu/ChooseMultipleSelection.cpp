#include "ChooseMultipleSelection.h"

#include <algorithm>

void ChooseMultipleSelection::reset(int newSelectionLimit)
{
	selectionLimit = std::max(0, newSelectionLimit);
	confirmed = false;
	selectedOptions.clear();
}

int ChooseMultipleSelection::limit() const
{
	return selectionLimit;
}

bool ChooseMultipleSelection::canSatisfyWithAvailableOptions(int availableOptionCount) const
{
	return selectionLimit > 0 && availableOptionCount >= selectionLimit;
}

bool ChooseMultipleSelection::canConfirm() const
{
	return selectionLimit > 0 && static_cast<int>(selectedOptions.size()) >= selectionLimit;
}

bool ChooseMultipleSelection::isConfirmed() const
{
	return confirmed;
}

bool ChooseMultipleSelection::isFinished() const
{
	return confirmed;
}

bool ChooseMultipleSelection::contains(int optionIndex) const
{
	return std::find(selectedOptions.begin(), selectedOptions.end(), optionIndex) != selectedOptions.end();
}

void ChooseMultipleSelection::toggle(int optionIndex)
{
	if (optionIndex < 0 || isFinished())
	{
		return;
	}

	auto it = std::find(selectedOptions.begin(), selectedOptions.end(), optionIndex);
	if (it != selectedOptions.end())
	{
		selectedOptions.erase(it);
		return;
	}

	if (static_cast<int>(selectedOptions.size()) < selectionLimit)
	{
		selectedOptions.push_back(optionIndex);
	}
}

bool ChooseMultipleSelection::confirm()
{
	if (!canConfirm())
	{
		return false;
	}

	confirmed = true;
	return true;
}

void ChooseMultipleSelection::clear()
{
	selectedOptions.clear();
}

const std::vector<int>& ChooseMultipleSelection::selections() const
{
	return selectedOptions;
}
