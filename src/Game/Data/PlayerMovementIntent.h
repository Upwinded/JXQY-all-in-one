#pragma once

inline bool shouldUseRunForPlayerMoveIntent(
	bool runRequested,
	int walkIsRun,
	bool runEnabled,
	bool canPayRunThewCost)
{
	return (runRequested || walkIsRun > 0) && runEnabled && canPayRunThewCost;
}
