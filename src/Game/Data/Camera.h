#pragma once
#include "GameElement.h"
#include <algorithm>
#include <cmath>

class Camera :
	public GameElement
{
public:
	Camera();
	virtual ~Camera();
private:
	float cameraSpeed = 40;

	const float flyRatio = 0.625 / 10.0;
	PointEx distanceToFly = { 0.0, 0.0 };
	PointEx distanceFlied = { 0.0, 0.0 };
	int flyDirection = 0;
	int flySpeed = 1;
	Point flyStartPosition = { 0, 0 };
	PointEx flyStartOffset = { 0.0, 0.0 };
	int directionalMoveDirection = 0;
	int directionalMoveFramesRemaining = 0;
	int directionalMoveSpeed = 0;
	PointEx directionalMoveRemainder = { 0.0, 0.0 };

public:
	struct DirectionalFrameMoveStep
	{
		Point distance = { 0, 0 };
		PointEx remainder = { 0.0, 0.0 };
	};

	std::weak_ptr<GameElement> followNPC;
	bool followPlayer = true;
	void flyTo(int dir, int distance, int speed);
	void flyToEx(int dir, int distance, int speed);
	void flyToPosition(int x, int y, int speed);
	void moveForFrameCount(int dir, int frameCount, int speed);

	void setFlyTo(int dir, int distance, int speed);
	void setFollowPlayer();

	// Recalculate position/offset from the current follow target and map bounds.
	// Does not change followPlayer/flying or clear differencePosition.
	void snapToFollowTarget();
	// Clamp the current camera position to map bounds, centering axes smaller than the screen.
	void clampToMapBounds();
	// Reset to player follow mode, stop camera flight, snap to target, and clear movement delta.
	void resetView();
	static int resolveVibrationDegree(int currentDegree, int requestedDegree)
	{
		if (requestedDegree <= 0)
		{
			return currentDegree;
		}
		return requestedDegree;
	}
	static int decayVibrationDegreeAfterFrame(int degree)
	{
		return degree > 0 ? degree - 1 : 0;
	}
	static float resolveVibrationAddition(float accumulatedOffset, int degree, float proposedAddition)
	{
		if (degree <= 0)
		{
			return 0.0f;
		}
		if (std::abs(accumulatedOffset) > static_cast<float>(degree))
		{
			return accumulatedOffset > 0.0f
				? -std::abs(proposedAddition)
				: std::abs(proposedAddition);
		}
		return proposedAddition;
	}
	static PointEx resolveVibrationOffsetAfterFrame(
		PointEx accumulatedOffset,
		int degree,
		PointEx proposedAddition)
	{
		if (degree <= 0 || decayVibrationDegreeAfterFrame(degree) == 0)
		{
			return { 0.0f, 0.0f };
		}
		return
		{
			accumulatedOffset.x + resolveVibrationAddition(accumulatedOffset.x, degree, proposedAddition.x),
			accumulatedOffset.y + resolveVibrationAddition(accumulatedOffset.y, degree, proposedAddition.y)
		};
	}
	static int normalizeFlySpeed(int speed)
	{
		return std::clamp(speed, 1, 1000);
	}
	static DirectionalFrameMoveStep calculateDirectionalFrameMoveStep(
		PointEx remainder,
		int direction,
		int speed)
	{
		const int normalizedDirection =
			direction >= 0 && direction <= 7 ? direction : 0;
		const float diagonal = 1.0f / std::sqrt(2.0f);
		const PointEx directions[8] =
		{
			{ 0.0f, 1.0f },
			{ -diagonal, diagonal },
			{ -1.0f, 0.0f },
			{ -diagonal, -diagonal },
			{ 0.0f, -1.0f },
			{ diagonal, -diagonal },
			{ 1.0f, 0.0f },
			{ diagonal, diagonal }
		};
		const float frameDistance =
			static_cast<float>(speed) * 2.0f;
		PointEx accumulated =
		{
			remainder.x + directions[normalizedDirection].x * frameDistance,
			remainder.y + directions[normalizedDirection].y * frameDistance
		};

		DirectionalFrameMoveStep step;
		if (std::abs(accumulated.x) >= 1.0f)
		{
			step.distance.x = static_cast<int>(accumulated.x);
			accumulated.x -= static_cast<float>(step.distance.x);
		}
		if (std::abs(accumulated.y) >= 1.0f)
		{
			step.distance.y = static_cast<int>(accumulated.y);
			accumulated.y -= static_cast<float>(step.distance.y);
		}
		step.remainder = accumulated;
		return step;
	}
	static bool isZeroFlightDistance(PointEx distance)
	{
		return std::hypot(distance.x, distance.y) <= 0.0001f;
	}
	void vibrate(int degree);

	void reArrangeNPC();
    PointEx differencePosition = { 0.0, 0.0 };

private:
	bool flying = false;
	int vibratingDegree = 0;
	PointEx vibrationOffset = { 0.0, 0.0 };
	void removeVibrationOffset();
	void clearVibrationOffset();
	void updateVibrationOffset();
	void clearDirectionalFrameMove();
	void updateDirectionalFrameMove();
	virtual void onUpdate();
	virtual void onEvent();
	void onWindowResize(int width, int height) override;
};
