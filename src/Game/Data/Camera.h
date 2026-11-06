#pragma once
#include "GameElement.h"
#include "ProjectedMovement.h"
#include <algorithm>
#include <cmath>

class Camera :
	public GameElement
{
	friend class OriginalMovementRuntimeTestAccess;
public:
	Camera();
	virtual ~Camera();
private:
	PointEx distanceToFly = { 0.0, 0.0 };
	PointEx distanceFlied = { 0.0, 0.0 };
	int flySpeed = 1;
	Point flyStartPosition = { 0, 0 };
	PointEx flyStartOffset = { 0.0, 0.0 };
	int directionalMoveDirection = 0;
	double directionalMoveFramesRemaining = 0.0;
	int directionalMoveSpeed = 0;
	PointBase<double> directionalMoveRemainder = { 0.0, 0.0 };
	bool csharpDirectionalMove = false;

public:
	struct DirectionalFrameMoveStep
	{
		Point distance = { 0, 0 };
		PointBase<double> remainder = { 0.0, 0.0 };
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
	static float clampHorizontalWorldPosition(float worldPosition, int mapColumnCount, int mapRowCount, int viewportWidth)
	{
		const float mapPixelWidth = static_cast<float>(mapRowCount <= 1
			? mapColumnCount - 1 : mapColumnCount) * TILE_WIDTH;
		const float minimumMapX = static_cast<float>(TILE_WIDTH) / 2.0f;
		const float maximumMapX = mapPixelWidth - minimumMapX;
		if (maximumMapX - minimumMapX <= static_cast<float>(viewportWidth))
		{
			return (minimumMapX + maximumMapX) / 2.0f;
		}
		const float halfViewportWidth = static_cast<float>(viewportWidth) / 2.0f;
		return std::clamp(worldPosition, minimumMapX + halfViewportWidth, maximumMapX - halfViewportWidth);
	}
	static float clampVerticalWorldPosition(float worldPosition, int mapRowCount, int viewportHeight)
	{
		// Inset every map edge by half a tile to exclude the staggered tile tips.
		const float minimumMapY = static_cast<float>(TILE_HEIGHT) / 2.0f;
		const float maximumMapY = static_cast<float>(mapRowCount - 1) * TILE_HEIGHT / 2.0f - minimumMapY;
		if (maximumMapY - minimumMapY <= static_cast<float>(viewportHeight))
		{
			return (minimumMapY + maximumMapY) / 2.0f;
		}
		const float halfViewportHeight = static_cast<float>(viewportHeight) / 2.0f;
		return std::clamp(worldPosition, minimumMapY + halfViewportHeight, maximumMapY - halfViewportHeight);
	}
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
		PointBase<double> remainder,
		int direction,
		int speed,
		double frameCount = 1.0,
		bool csharpProtocol = false)
	{
		const int normalizedDirection =
			direction >= 0 && direction <= 7 ? direction : 0;
		const PointBase<double> directions[8] =
		{
			{ 0.0f, 1.0f },
			{ -2.0f, 1.0f },
			{ -2.0f, 0.0f },
			{ -2.0f, -1.0f },
			{ 0.0f, -1.0f },
			{ 2.0f, -1.0f },
			{ 2.0f, 0.0f },
			{ 2.0f, 1.0f }
		};
		auto movementDirection = directions[normalizedDirection];
		if (csharpProtocol)
		{
			movementDirection.x /= 2.0;
			const double length = std::hypot(movementDirection.x, movementDirection.y);
			movementDirection.x *= 2.0 / length;
			movementDirection.y *= 2.0 / length;
		}
		const double frameDistance = static_cast<double>(speed) * std::max(0.0, frameCount);
		PointBase<double> accumulated =
		{
			remainder.x + movementDirection.x * frameDistance,
			remainder.y + movementDirection.y * frameDistance
		};
		const auto snapInteger = [](double value)
		{
			const double nearest = std::round(value);
			return std::abs(value - nearest) < 0.000000001 ? nearest : value;
		};
		accumulated.x = snapInteger(accumulated.x);
		accumulated.y = snapInteger(accumulated.y);

		DirectionalFrameMoveStep step;
		if (std::abs(accumulated.x) >= 1.0f)
		{
			step.distance.x = static_cast<int>(accumulated.x);
			accumulated.x -= step.distance.x;
		}
		if (std::abs(accumulated.y) >= 1.0f)
		{
			step.distance.y = static_cast<int>(accumulated.y);
			accumulated.y -= step.distance.y;
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
	void setDirectionalFrameMove(int direction, double frames, int speed, bool csharpProtocol);
	void updateDirectionalFrameMove(UTime frameTime);
	virtual void onUpdate();
	virtual void onEvent();
	void onWindowResize(int width, int height) override;
};
