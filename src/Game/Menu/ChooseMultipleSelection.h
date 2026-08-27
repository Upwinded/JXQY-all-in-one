#pragma once

#include <vector>

class ChooseMultipleSelection
{
public:
	void reset(int selectionLimit);
	int limit() const;
	bool canSatisfyWithAvailableOptions(int availableOptionCount) const;
	bool canConfirm() const;
	bool isConfirmed() const;
	bool isFinished() const;

	bool contains(int optionIndex) const;
	void toggle(int optionIndex);
	bool confirm();
	void clear();

	const std::vector<int>& selections() const;

private:
	int selectionLimit = 0;
	bool confirmed = false;
	std::vector<int> selectedOptions;
};
