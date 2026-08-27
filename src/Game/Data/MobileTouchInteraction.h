#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

constexpr uint64_t MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS = 600;
constexpr int MOBILE_RIGHT_SCRIPT_MOVE_TOLERANCE_PIXELS = 24;
constexpr float MOBILE_JOYSTICK_DEAD_ZONE_RATIO = 1.0f / 20.0f;
constexpr float MOBILE_JOYSTICK_RUN_ZONE_RATIO = 3.0f / 20.0f;
constexpr double MOBILE_JOYSTICK_PI = 3.14159265358979323846;

inline bool shouldDeferMobileRightScriptChoice(const std::string& scriptFile, const std::string& scriptFileRight)
{
	return !scriptFile.empty() && !scriptFileRight.empty();
}

inline bool shouldUseMobileRightScript(
	uint64_t pressMilliseconds,
	int moveDeltaX,
	int moveDeltaY)
{
	if (pressMilliseconds < MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS)
	{
		return false;
	}

	const int64_t tolerance = MOBILE_RIGHT_SCRIPT_MOVE_TOLERANCE_PIXELS;
	const int64_t deltaX = moveDeltaX;
	const int64_t deltaY = moveDeltaY;
	return deltaX * deltaX + deltaY * deltaY <= tolerance * tolerance;
}

inline double normalizeMobileJoystickAngle(double angle)
{
	while (angle < -MOBILE_JOYSTICK_PI)
	{
		angle += 2.0 * MOBILE_JOYSTICK_PI;
	}
	while (angle > MOBILE_JOYSTICK_PI)
	{
		angle -= 2.0 * MOBILE_JOYSTICK_PI;
	}
	return angle;
}

inline int normalizeMobileJoystickDirection(int direction)
{
	return ((direction % 8) + 8) % 8;
}

inline int getMobileJoystickDistance(int deltaX, int deltaY)
{
	return static_cast<int>(std::round(std::hypot(deltaX, deltaY)));
}

inline bool isMobileJoystickDirectionActive(int deltaX, int deltaY, int range)
{
	if (range <= 0)
	{
		return false;
	}
	return getMobileJoystickDistance(deltaX, deltaY) >= range * MOBILE_JOYSTICK_DEAD_ZONE_RATIO;
}

inline bool isMobileJoystickWalking(int deltaX, int deltaY, int range)
{
	if (range <= 0)
	{
		return false;
	}
	int distance = getMobileJoystickDistance(deltaX, deltaY);
	return distance > range * MOBILE_JOYSTICK_DEAD_ZONE_RATIO
		&& distance <= range * MOBILE_JOYSTICK_RUN_ZONE_RATIO;
}

inline bool isMobileJoystickRunning(int deltaX, int deltaY, int range)
{
	if (range <= 0)
	{
		return false;
	}
	return getMobileJoystickDistance(deltaX, deltaY) > range * MOBILE_JOYSTICK_RUN_ZONE_RATIO;
}

inline double getMobileJoystickAngle(int deltaX, int deltaY)
{
	return std::atan2(-deltaX, deltaY);
}

inline int getMobileJoystickPrimaryDirection(int deltaX, int deltaY)
{
	double angle = getMobileJoystickAngle(deltaX, deltaY);
	if (angle < 0.0)
	{
		angle += 2.0 * MOBILE_JOYSTICK_PI;
	}
	angle += MOBILE_JOYSTICK_PI / 8.0;
	if (angle > 2.0 * MOBILE_JOYSTICK_PI)
	{
		angle -= 2.0 * MOBILE_JOYSTICK_PI;
	}
	return normalizeMobileJoystickDirection(static_cast<int>(angle / (MOBILE_JOYSTICK_PI / 4.0)));
}

inline std::vector<int> getMobileJoystickDirectionCandidates(int deltaX, int deltaY, int range)
{
	std::vector<int> directionList;
	if (!isMobileJoystickDirectionActive(deltaX, deltaY, range))
	{
		return directionList;
	}

	int direction = getMobileJoystickPrimaryDirection(deltaX, deltaY);
	directionList.push_back(direction);

	double angle = getMobileJoystickAngle(deltaX, deltaY);
	double directionAngle = static_cast<double>(direction) * MOBILE_JOYSTICK_PI / 4.0;
	double angleDelta = normalizeMobileJoystickAngle(angle - directionAngle);
	if (angleDelta >= 0.0)
	{
		directionList.push_back(normalizeMobileJoystickDirection(direction + 1));
		directionList.push_back(normalizeMobileJoystickDirection(direction - 1));
	}
	else
	{
		directionList.push_back(normalizeMobileJoystickDirection(direction - 1));
		directionList.push_back(normalizeMobileJoystickDirection(direction + 1));
	}
	return directionList;
}
