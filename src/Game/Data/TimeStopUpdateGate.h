#pragma once

inline bool shouldUpdateGameManagerChildDuringTimeStop(bool hasActiveTimeStopper, bool isWeatherChild)
{
	return !hasActiveTimeStopper || !isWeatherChild;
}

inline bool shouldUpdateEffectManagerChildDuringTimeStop(bool hasActiveTimeStopper, bool isActiveTimeStopperEffect)
{
	return !hasActiveTimeStopper || isActiveTimeStopperEffect;
}

inline bool shouldUpdateNpcManagerChildDuringTimeStop(bool hasActiveTimeStopper, bool isTimeStopperUser)
{
	return !hasActiveTimeStopper || isTimeStopperUser;
}

inline bool shouldUpdateObjectManagerChildDuringTimeStop(bool hasActiveTimeStopper)
{
	return !hasActiveTimeStopper;
}

inline bool shouldUpdateGameControllerChildDuringTimeStop(
	bool hasActiveTimeStopper,
	bool isObjectManager,
	bool isPlayer,
	bool isTimeStopperUser)
{
	if (!hasActiveTimeStopper)
	{
		return true;
	}
	if (isObjectManager)
	{
		return false;
	}
	if (isPlayer)
	{
		return isTimeStopperUser;
	}
	return true;
}
